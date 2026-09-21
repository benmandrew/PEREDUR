#include "muc_mode.hpp"

#include <algorithm>
#include <cstddef>
#include <iostream>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "config.hpp"
#include "dashboard.hpp"
#include "evolution.hpp"
#include "fitness/function.hpp"
#include "genetic/accumulator.hpp"
#include "genetic/filter_report.hpp"
#include "genetic/generation.hpp"
#include "genetic/output_gate.hpp"
#include "genetic/pipeline.hpp"
#include "genetic/random_source.hpp"
#include "mucs.hpp"
#include "requirement.hpp"
#include "runner/black.hpp"
#include "runner/spot.hpp"
#include "serialisation.hpp"
#include "thread_pool.hpp"

namespace {

std::optional<bool> whole_spec_realizable(const Specification& spec) {
    if (count_live(spec.m_guarantees) == 0) {
        return true;
    }
    return global_real_checker().check_realizability(spec);
}

std::string slot_list(const std::vector<std::size_t>& slots) {
    std::string out = "{";
    for (std::size_t idx = 0; idx < slots.size(); ++idx) {
        out += (idx == 0 ? "" : ", ") + std::to_string(slots[idx]);
    }
    return out + "}";
}

// Appends @p spec to @p kept unless @p seen already holds it.
bool keep_distinct(const Specification& spec,
                   std::unordered_set<Specification>& seen,
                   std::vector<Specification>& kept) {
    if (!seen.insert(spec).second) {
        return false;
    }
    kept.push_back(spec);
    return true;
}

// Everything one core's search yields, gated as repairs of the whole.
struct GatedCore {
    std::vector<Specification> rejoined;
    std::vector<Specification> passed;
    std::vector<Specification> provisional;
    // Gate checks that never answered, the deadline's included.
    std::size_t n_undecided = 0;
    // Candidates the deadline left without a verdict: never gated, or
    // undecided and not screened to the end. None is provisional.
    std::size_t n_unscreened = 0;
};

// Evolves the core sub-specification from a population of copies of it, and
// returns its distinct gate-passing survivors, each realizable against the
// core alone, best first under @p core_cfg's selection order, so a deadline
// that cuts the whole-spec gate short has judged the fittest first. @p core_cfg
// must not accumulate.
std::vector<Specification> evolve_core(const Config& core_cfg,
                                       Specification core_spec,
                                       RandomSource& random_source,
                                       DashboardWriter& dashboard,
                                       const std::string& output_dir,
                                       SearchBudget& budget,
                                       std::vector<FilterRunStats>& stats) {
    const AggregateWeightedFitnessFunction fitness =
        get_fitness_function(core_spec, core_cfg);
    const std::vector<FilterFunction> filters =
        get_filter_functions(core_spec, global_sat_checker());
    std::vector<ScoredSpecification> seed =
        original_population(core_spec, fitness, core_cfg.population_size);
    EvolutionResult evolved =
        run_evolution(core_cfg, std::move(seed), fitness, filters,
                      random_source, dashboard, output_dir, budget);
    accumulate_filter_stats(stats, evolved.filter_stats);
    order_population(core_cfg, evolved.population);
    std::vector<Specification> survivors;
    std::unordered_set<Specification> seen;
    for (const Specification& spec :
         collect_realizable_specifications(core_cfg, evolved.population)) {
        keep_distinct(spec, seen, survivors);
    }
    return survivors;
}

// Puts each core survivor back into @p context and gates the result against
// the original. A candidate the gate could not judge is provisional where
// every guarantee subset of up to @p screen_depth is decided realizable: sound
// up to that depth, and silent about any conflict larger.
//
// Both the gate and the screens launch in survivor order, which is fitness
// order, and neither starts new work once @p stop holds. A candidate left
// without a verdict then is undecided, unscreened, and not provisional.
GatedCore gate_rejoined(const std::vector<Specification>& core_survivors,
                        const Specification& context,
                        const fretish::GuaranteeCore& core, const Config& cfg,
                        const fretish::RealizabilityVerdict& verdict,
                        std::size_t window,
                        const fretish::StopRequested& stop) {
    GatedCore gated;
    std::vector<ScoredSpecification> rejoined;
    rejoined.reserve(core_survivors.size());
    for (const Specification& repaired : core_survivors) {
        ScoredSpecification candidate;
        candidate.specification =
            fretish::reintegrate(repaired, context, core.slots);
        gated.rejoined.push_back(candidate.specification);
        rejoined.push_back(std::move(candidate));
    }
    const std::vector<std::optional<GateVerdict>> verdicts =
        output_gate_verdicts(rejoined, cfg, stop);
    for (std::size_t idx = 0; idx < rejoined.size(); ++idx) {
        const Specification& spec = rejoined[idx].specification;
        const std::optional<GateVerdict>& judged = verdicts[idx];
        if (!judged.has_value()) {
            ++gated.n_undecided;
            ++gated.n_unscreened;
            continue;
        }
        if (judged.value() == GateVerdict::Pass) {
            gated.passed.push_back(spec);
        } else if (judged.value() == GateVerdict::Undecided) {
            ++gated.n_undecided;
            const fretish::SubsetScreen screen = fretish::screen_small_subsets(
                spec, verdict, cfg.muc_screen_depth, window, stop);
            if (screen.interrupted) {
                ++gated.n_unscreened;
            } else if (screen.all_decided_realizable) {
                gated.provisional.push_back(spec);
            }
        }
    }
    return gated;
}

// Says why the loop stops when @p core gives it nothing to repair, and returns
// false then. @p head_decided is whether the head check answered.
bool core_to_repair(const fretish::GuaranteeCore& core, bool head_decided) {
    if (core.interrupted) {
        std::cout << "muc: deadline reached during core extraction; stopping\n";
        return false;
    }
    if (core.slots.empty()) {
        std::cout << (head_decided ? "muc: no guarantee-side core to repair; "
                                     "stopping\n"
                                   : "muc: no conflict among small guarantee "
                                     "subsets; stopping\n");
        return false;
    }
    return true;
}

void report_core(const fretish::GuaranteeCore& core, std::size_t iter,
                 const Config& cfg) {
    std::cout << "muc iteration " << (iter + 1) << "/" << cfg.muc_max_iterations
              << ": core of " << core.slots.size() << " guarantee(s), slots "
              << slot_list(core.slots) << "\n";
    if (core.n_undecided > 0) {
        std::cout << "muc: " << core.n_undecided << " undecided probe(s)"
                  << (core.provisional ? "; core is provisional" : "") << "\n";
    }
}

// Onward from a confirmed repair where there is one, so the next head check
// ends the loop; else from a provisional one, whose next head check is
// undecided and whose small subsets already screened clean; else from the
// first reintegration, whose core is repaired even if its context is not.
const Specification& next_context(const GatedCore& gated) {
    if (!gated.passed.empty()) {
        return gated.passed.front();
    }
    if (!gated.provisional.empty()) {
        return gated.provisional.front();
    }
    return gated.rejoined.front();
}

}  // namespace

MucRepairResult run_fretish_muc(const Config& cfg,
                                const Specification& original,
                                RandomSource& random_source,
                                DashboardWriter& dashboard,
                                const std::string& output_dir,
                                SearchBudget& budget,
                                RepairAccumulator<Specification>::Sink sink) {
    // The same writer run_evolution builds, so an accumulated file here is
    // the document it would be on the monolithic path. What reaches it is a
    // reintegrated whole specification that passed the gate.
    RepairAccumulator<Specification> accumulator(
        cfg.accumulate_repairs,
        AccumulatedRepairWriter<Specification>(
            output_dir, ".json",
            [](const Specification& spec) {
                const nlohmann::json jobj = spec;
                return jobj.dump(2) + "\n";
            },
            [&budget] { return budget.elapsed_s(); }),
        std::move(sink));
    // A gate-passing candidate of a core is realizable against the core alone,
    // so accumulating one would report a fragment as a repair of the whole.
    Config core_cfg = cfg;
    core_cfg.accumulate_repairs = false;

    const fretish::RealizabilityVerdict verdict =
        [](const Specification& spec) {
            return global_real_checker().check_realizability(spec);
        };
    const std::size_t window = dispatch_window();
    // The wall budget bounds the whole run, not only the search: past it, the
    // extraction, the gate and the screens start nothing new. Work already
    // running finishes, each tool call within its own timeout.
    const fretish::StopRequested past_deadline = [&budget] {
        return budget.past_deadline();
    };

    MucRepairResult result;
    std::unordered_set<Specification> seen_repairs;
    std::unordered_set<Specification> seen_provisional;
    Specification current = original;
    std::size_t gen_offset = 0;
    for (std::size_t iter = 0; iter < cfg.muc_max_iterations; ++iter) {
        // The budget is the run's, not the core's, as on TLSF.
        if (budget.active() && budget.exhausted()) {
            std::cout << "muc: search budget exhausted; stopping\n";
            break;
        }
        const std::optional<bool> head = whole_spec_realizable(current);
        if (head.value_or(false)) {
            break;
        }
        if (!head.has_value()) {
            std::cout << "muc: whole-spec realizability undecided; screening "
                         "guarantee subsets of up to "
                      << cfg.muc_screen_depth << "\n";
        }
        // The walk only on a decided conflict: QuickXplain assumes the whole
        // set is one, and reads every undecided probe as one besides.
        const fretish::GuaranteeCore core = fretish::extract_core(
            current, verdict, cfg.muc_screen_depth, window,
            /*allow_walk=*/head.has_value(), past_deadline);
        if (!core_to_repair(core, head.has_value())) {
            break;
        }
        report_core(core, iter, cfg);
        const std::vector<Specification> core_survivors =
            evolve_core(core_cfg, core.spec, random_source, dashboard,
                        output_dir, budget, result.filter_stats);
        gen_offset += cfg.generations;
        if (core_survivors.empty()) {
            std::cout << "muc: core could not be made realizable; stopping\n";
            break;
        }
        const GatedCore gated = gate_rejoined(
            core_survivors, current, core, cfg, verdict, window, past_deadline);
        for (const Specification& spec : gated.passed) {
            if (keep_distinct(spec, seen_repairs, result.repairs)) {
                accumulator.insert(spec, gen_offset);
            }
        }
        for (const Specification& spec : gated.provisional) {
            keep_distinct(spec, seen_provisional, result.provisional);
        }
        MucStats::n_gate_undecided += gated.n_undecided;
        MucStats::n_deadline_unscreened += gated.n_unscreened;
        std::cout << "muc: " << gated.rejoined.size() << " reintegrated, "
                  << gated.passed.size() << " passed the gate, "
                  << gated.n_undecided << " undecided ("
                  << gated.provisional.size() << " provisional, "
                  << gated.n_unscreened << " unscreened at the deadline)\n";
        current = next_context(gated);
    }
    if (result.repairs.empty() && !budget.past_deadline() &&
        output_gate_verdict(current, cfg) == GateVerdict::Pass) {
        // An input that was realizable to begin with, as on TLSF.
        result.repairs.push_back(current);
        seen_repairs.insert(current);
    }
    // A provisional specification that a later check confirmed is a repair,
    // and only that.
    result.provisional.erase(
        std::remove_if(result.provisional.begin(), result.provisional.end(),
                       [&seen_repairs](const Specification& spec) {
                           return seen_repairs.count(spec) != 0;
                       }),
        result.provisional.end());
    result.accumulated = accumulator.specifications();
    return result;
}
