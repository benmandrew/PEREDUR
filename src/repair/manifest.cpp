#include "manifest.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

#include <nlohmann/json.hpp>

#include "config/enum_names.hpp"
#include "config/keys.hpp"
#include "filter/correctness.hpp"
#include "filter/implication.hpp"
#include "filter/well_separation.hpp"
#include "fitness/function.hpp"
#include "fitness/semantic_similarity.hpp"
#include "genetic/accumulator.hpp"
#include "runner/black.hpp"
#include "runner/ganak.hpp"
#include "runner/ltlfilt.hpp"
#include "runner/spot.hpp"
#include "version.hpp"

template <>
struct EnumNames<StopReason> {
    static constexpr std::array<std::pair<StopReason, const char*>, 3> k_names{
        {{StopReason::Generations, "generations"},
         {StopReason::Individuals, "individuals"},
         {StopReason::Deadline, "deadline"}}};
};

namespace {

// Bumped when a key changes meaning or leaves, so a reader can tell which
// shape it is holding rather than guessing from which keys are present.
// 2 added fitness_cache, n_constant_folded and n_tautology_substitutions, when
// those counters stopped printing to stdout by default and this became their
// only unconditional record. Anything print_diagnostics_report shows has to
// appear here too, or turning the flag off loses it.
// 3 added input_screen, so a campaign can partition its runs on whether the
// input itself failed a correctness check rather than grepping the log for the
// warning.
// 4 added n_weak_operator_unresolved. It counts satisfiability queries
// abandoned rather than answered, so a run that reports repairs with a
// non-zero count screened less than a run reporting the same repairs with
// zero -- which is not visible from any other field.
// 5 added budget_screen, so a zero-yield run can be told apart from one whose
// ltlsynt budget could not decide its own input specification.
// 6 added fitness.status_grading, since the status objective now has two
// scales and a manifest that does not name the one in use cannot be read.
// 7 dropped fitness.weight_halstead along with the objective it weighted, so
// the objective vector is one shorter and a reader keying on its position
// against an older manifest reads the wrong component.
// 8 added mutation.p_remove_guarantee. The operator can delete a guarantee, so
// a repair may hold fewer requirements than the specification it came from, and
// a reader comparing counts needs to know whether the run could do that.
// 9 added n_spot_decided and n_escalations_declined, and redefined
// tool_calls.black.calls as black's own exec count rather than the
// satisfiability cache-miss count, which SPOT taking the first stage split in
// two. Comparing black.calls across the boundary compares different things.
// 10 added n_well_separation_errors. The well-separation query used to let an
// ltlsynt error end the run, so a run that finished implies zero of them; from
// this version a finished run may have dropped candidates on a non-answer, and
// only this field says how many.
// 11 added input_screen_error. Before it, a screen that raised ended the run
// before any manifest was written, so `input_screen: null` meant the input
// passed every check. From this version the run survives, and null means either
// that or a screen that never produced a verdict -- this field is what tells
// them apart.
// 12 added fitness.mrs_admission_order. The MRS walk's score depends on the
// order it admits parts in, so two runs on one specification under one grading
// scale are not comparable without it.
// 13 added genetic.accumulate_repairs and n_accumulated_repairs. Before it a
// run's repairs all came from its final population, so the output was a
// function of the last generation alone; from this version the key can union
// in the repairs earlier generations found, and only this field says how many
// of them the run would otherwise have thrown away.
//
// 14 added genetic.repaired_operators. It selected between two mutation and
// crossover grammars with different RNG draw streams, so two runs of the same
// seed agreed only when they agreed on this key.
//
// 15 removed genetic.repaired_operators. The repaired grammar is now the only
// one, so the key no longer selects anything; a manifest at this version or
// later was written by a binary that has only that grammar.
//
// 16 added n_ltlsynt_capability_errors. Before it, an ltlsynt run that reported
// a limit of its own instead of a verdict ended the run and no manifest was
// written at all; from this version the query resolves as undecided and this
// field is the only record that it happened.
// 20 redefined tool_calls.ltlfilt.calls and added the caches block. `calls`
// counted simplify_ltl's and rewrite_weak_operators' cache misses, while
// `total_s` beside it also covered every spot_satisfiable exec, which is
// memoised nowhere -- so the field understated ltlfilt's true exec count by
// 35.5% over a 14-specification corpus and the seconds per call derived from
// the pair were over two different sets. It is now the exec count total_s is
// actually over. `caches` reports the hits and misses of every memo in the
// run, which no field carried before: the satisfiability cache's miss count
// reached no field at all (the black row reports black's own exec count,
// deliberately, since SPOT takes the first stage), and it is the largest cache
// in a run at 237,969 lookups over that corpus.
//
// 21 added stopped_by, generations_run and individuals_bred, with
// genetic.termination, genetic.max_individuals and genetic.max_wall_s beside
// them in the config block. A run could end only by exhausting
// genetic.generations before this version, so its wall time and its generation
// count said everything there was to say about how it stopped; from this
// version it can also spend an offspring budget or pass a deadline, and only
// stopped_by separates the three. individuals_bred is null wherever no budget
// was active, an unbudgeted run not paying for the count.
//
// 22 added mutation.p_condition_type and mutation.p_scope, the two directional
// arms over a requirement's scope and condition type.
//
// 23 added the implication block. The pairwise sweep's counters reached no
// field at all before it, so a run's diagnostics carried them and a campaign
// reading run.json could not: n_equivalent_collapsed in particular is the only
// record of how much of the output was one repair written twice, and it is
// unreadable without n_comparisons beside it to say how many pairs were asked.
//
// 24 added "aurus" to the values fitness.status_grading can take. It exists to
// be ablated against rather than to be run: it reproduces AuRUS's own ladder so
// a campaign can cross PEREDUR's grading against the design PEREDUR derives
// from, and it is the one scale that does not cap a candidate gaming its own
// assumptions. A reader keying on that field against an older manifest can
// assume it names one of two scales that do; from this version it may name a
// third that does not, so the status objective of two runs is comparable only
// where the field agrees.
//
// 25 added mutation.p_monotone, the FRETISH monotone rewrite arm. Its TLSF
// twin tlsf.mutation.p_monotone has been in the config block since the block
// existed, so a reader keying on the name alone now finds two.
//
// 26 added implication.fingerprint_refuted, the ordered pairs a sampled word
// settled ahead of the solver. It is the only reading of whether the prefilter
// worked on a given run, the pairs it refutes never reaching `comparisons`.
// The same version is where the FRETISH implication check stopped decomposing
// per requirement, so `comparisons`, `timeouts` and the repair counts beside
// them are not comparable across it on that path.
//
// 27 removed tlsf.mutation.p_monotone (folded into mutation.p_monotone, which
// both paths now read), tlsf.mutation.connective_implies and the two
// tlsf.mutation.monotone_*_rules gates (all now unconditional), and
// filters.run_weakening and filters.run_well_separation (the stages are gone).
// It also removed filters.run_vacuity (vacuity always runs per generation),
// mutation.allow_output_assumptions (always allowed),
// tlsf.mutation.p_remove_assumption and p_burst_continue (their operators are
// gone), runtime.ganak_timeout_ms (ganak runs untimed) and
// runtime.max_concurrent_realizability (no ltlsynt concurrency cap), and
// dropped tool_calls.ganak.timeouts with the timeout it counted.
// The same version moved the fitness weights to AuRUS's 0.1/0.2/0.7 and
// mutation.p_condition_type, p_scope and p_monotone off 0.
//
// 28 moved both paths onto one implication sweep and runs it during the
// search. On TLSF, `comparisons`, `skipped` and `duplicates` read 0 on every
// earlier manifest, the TLSF sweep having counted none of them, and
// `fingerprint_refuted` was never reset; all four now carry the sweep's own
// figures, as they already did on FRETISH. `timeouts` still reads 0 there,
// tlsf_spec_implies counting none. Wherever the run accumulates, the
// implication figures are the streamed sweep's, whose batches ask the pairs
// within a batch and between it and the maximal set so far, so `comparisons`
// and `skipped` do not compare with any earlier run's; `duplicates` reads 0,
// the stream deduplicating before its sweep sees anything. No key selects
// between the two routes, so a manifest at 28 or above carries the streamed
// figures whenever accumulate_repairs and filters.run_implication are both on.
//
// 29 added mutation.p_stop, the FRETISH arm rewriting the stop condition of an
// `until` or `before` timing. It draws nothing on a specification without one,
// so an earlier run's search is what it would have been at any value.
//
// 30 counts implication.timeouts on TLSF, where tlsf_spec_implies counted none
// and the field read 0 on every earlier TLSF manifest.
constexpr int k_schema_version = 30;

std::string utc_timestamp() {
    const std::time_t now =
        std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm parts{};
    if (gmtime_r(&now, &parts) == nullptr) {
        return "";
    }
    std::array<char, 32> buffer{};
    if (std::strftime(buffer.data(), buffer.size(), "%Y-%m-%dT%H:%M:%SZ",
                      &parts) == 0) {
        return "";
    }
    return {buffer.data()};
}

// Counts distinct repair indices rather than files: the FRETISH path writes
// one repair_N.json per repair and the TLSF path writes repair_N.tlsf beside a
// repair_N.fitness.json, so counting files would double the TLSF total.
std::size_t count_repairs(const std::filesystem::path& dir) {
    std::error_code error;
    std::set<std::string> indices;
    for (const auto& entry : std::filesystem::directory_iterator(dir, error)) {
        const std::string name = entry.path().filename().string();
        if (name.rfind("repair_", 0) != 0) {
            continue;
        }
        const std::size_t dot = name.find('.');
        if (dot == std::string::npos) {
            continue;
        }
        indices.insert(name.substr(0, dot));
    }
    return indices.size();
}

nlohmann::json config_json(const Config& cfg) {
    // Mirrors the TOML section layout so a manifest diffs directly against a
    // config file rather than needing a key-by-key translation.
    nlohmann::json out = nlohmann::json::object();
    for (const ConfigKey& entry : k_config_keys) {
        nlohmann::json* section = &out;
        for (const std::string_view name : section_path(entry.section)) {
            section = &(*section)[std::string(name)];
        }
        nlohmann::json& slot = (*section)[entry.key];
        std::visit(
            [&](auto member) {
                const auto& field = cfg.*member;
                using Field = std::decay_t<decltype(field)>;
                if constexpr (std::is_same_v<Field,
                                             std::chrono::milliseconds>) {
                    slot = field.count();
                } else if constexpr (std::is_enum_v<Field>) {
                    slot = enum_name(field);
                } else {
                    slot = field;
                }
            },
            entry.member);
    }
    return out;
}

nlohmann::json tool_calls_json() {
    auto row = [](std::size_t calls, std::size_t cache_hits,
                  std::size_t timeouts, double total_s) {
        return nlohmann::json{{"calls", calls},
                              {"cache_hits", cache_hits},
                              {"timeouts", timeouts},
                              {"total_s", total_s}};
    };
    return {
        {"ltl2tgba",
         row(Ltl2tgbaStats::n_cache_misses, Ltl2tgbaStats::n_cache_hits,
             Ltl2tgbaStats::n_timeouts, Ltl2tgbaStats::total_time_s)},
        // Every ltlfilt exec, over all three entry points, which is the
        // set total_s is over. Its per-entry-point split is in `caches`.
        {"ltlfilt", row(LtlfiltStats::n_execs(), LtlfiltStats::n_cache_hits,
                        LtlfiltStats::n_timeouts, LtlfiltStats::total_time_s)},
        {"ltlsynt", row(RealizabilityChecker::n_cache_misses,
                        RealizabilityChecker::n_cache_hits,
                        RealizabilityChecker::n_timeouts,
                        RealizabilityChecker::total_time_s)},
        // "calls" is black's own exec count, which parted company with the
        // cache-miss count when SPOT took the first stage: a miss now
        // reaches black only if SPOT left it undecided and its polarity
        // says black could still answer.
        {"black", row(SatisfiabilityChecker::n_black_calls,
                      SatisfiabilityChecker::n_cache_hits,
                      SatisfiabilityChecker::n_timeouts,
                      SatisfiabilityChecker::total_time_s)},
        // No timeouts field: ganak runs untimed, so it could only ever read 0.
        {"ganak",
         {{"calls", GanakStats::n_cache_misses},
          {"cache_hits", GanakStats::n_cache_hits},
          {"total_s", GanakStats::total_time_s}}}};
}

// Every memoisation cache in the run, so that a hit rate is derivable for each
// rather than for the four that happened to share a name with a tool. The
// counts are lookups, not execs: several of these sit in front of one tool and
// a miss in one is not a subprocess in another.
// The pairwise implication sweep's own counters. `comparisons` is the
// denominator every other field is read against: `skipped` are the pairs an
// earlier verdict already settled, `duplicates` the specs excluded from the
// sweep outright, and `timeouts` the checks that gave no verdict and so read
// as non-implication. `equivalent_collapsed` counts the pairs found mutually
// equivalent, a class of k members contributing k-1. `fingerprint_refuted`
// counts ordered pairs, two per unordered pair, so a pair both directions of
// which a sampled word refuted never reaches `comparisons` at all.
nlohmann::json implication_json() {
    return {{"comparisons", ImplicationFilterStats::n_comparisons.load()},
            {"skipped", ImplicationFilterStats::n_skipped.load()},
            {"duplicates", ImplicationFilterStats::n_duplicates.load()},
            {"timeouts", ImplicationFilterStats::n_timeouts.load()},
            {"equivalent_collapsed",
             ImplicationFilterStats::n_equivalent_collapsed.load()},
            {"fingerprint_refuted",
             ImplicationFilterStats::n_fingerprint_refuted.load()}};
}

nlohmann::json caches_json() {
    auto row = [](std::size_t hits, std::size_t misses) {
        return nlohmann::json{{"hits", hits}, {"misses", misses}};
    };
    return {
        {"fitness",
         row(FitnessCacheStats::n_hits, FitnessCacheStats::n_misses)},
        {"satisfiability", row(SatisfiabilityChecker::n_cache_hits,
                               SatisfiabilityChecker::n_cache_misses)},
        {"realizability",
         nlohmann::json{{"hits", RealizabilityChecker::n_cache_hits},
                        {"misses", RealizabilityChecker::n_cache_misses},
                        // Answered from the monotone subsumption table rather
                        // than by an exec. Not a hit: a hit is the same
                        // question asked twice.
                        {"subsumed", RealizabilityChecker::n_subsumed}}},
        {"count_traces",
         row(CountTracesStats::n_hits, CountTracesStats::n_misses)},
        {"ltl2tgba",
         row(Ltl2tgbaStats::n_cache_hits, Ltl2tgbaStats::n_cache_misses)},
        {"ganak", row(GanakStats::n_cache_hits, GanakStats::n_cache_misses)},
        {"simplify_ltl",
         row(LtlfiltStats::n_cache_hits, LtlfiltStats::n_cache_misses)},
        {"remove_wm",
         row(LtlfiltStats::n_remove_wm_hits, LtlfiltStats::n_remove_wm_execs)},
        // Memoised nowhere: check_satisfiability caches the decided answer one
        // layer up, so a repeat never reaches it. Reported with no hits rather
        // than omitted, so that a reader summing execs finds it.
        {"spot_satisfiable", row(0, LtlfiltStats::n_satisfiable_execs)}};
}

nlohmann::json fitness_cache_json() {
    return {{"hits", FitnessCacheStats::n_hits},
            {"misses", FitnessCacheStats::n_misses}};
}

}  // namespace

void write_run_manifest(const std::string& output_dir,
                        const std::string& input_path, std::size_t seed,
                        const Config& cfg, double wall_s,
                        const SearchBudget& budget) {
    const std::filesystem::path dir(output_dir);
    const nlohmann::json manifest{
        {"schema_version", k_schema_version},
        {"tool", "peredur"},
        {"commit", version::commit()},
        {"commit_short", version::commit_short()},
        {"dirty", version::dirty()},
        {"finished_utc", utc_timestamp()},
        {"wall_s", wall_s},
        // Which budget ended the run, the generations it completed, and the
        // offspring it bred getting there. individuals_bred is null under the
        // default configuration: counting an offspring means comparing it
        // against the parent it was bred from, which an unbudgeted run does not
        // pay for, so a zero here would read as a fact rather than as an
        // absence.
        {"stopped_by", enum_name(budget.reason(StopReason::Generations))},
        {"generations_run", budget.generations()},
        {"individuals_bred", budget.active() ? nlohmann::json(budget.bred())
                                             : nlohmann::json(nullptr)},
        {"input", input_path},
        {"seed", seed},
        {"n_repairs", count_repairs(dir)},
        // Null when the input passed every check, which is the ordinary case;
        // the name of the check it failed otherwise.
        {"input_screen", InputScreen::failed_check.empty()
                             ? nlohmann::json(nullptr)
                             : nlohmann::json(InputScreen::failed_check)},
        // Null unless a tool raised while screening, in which case
        // `input_screen` above is null for want of a verdict rather than
        // because the input passed. A campaign partitioning on `input_screen`
        // must read this key too, or it counts an unscreened run as a clean
        // one.
        {"input_screen_error", InputScreen::error.empty()
                                   ? nlohmann::json(nullptr)
                                   : nlohmann::json(InputScreen::error)},
        // Null when no screen ran, which is the case under an unlimited
        // budget. `decided` false means the budget could not settle the input
        // specification's own realizability, so a campaign can partition its
        // zero-yield rows on the budget rather than read them as search
        // failures. `observed_ms` is a lower bound when undecided: the child
        // was killed at the budget.
        {"budget_screen",
         BudgetScreen::observed_ms < 0
             ? nlohmann::json(nullptr)
             : nlohmann::json({{"ltlsynt_ms", BudgetScreen::observed_ms},
                               {"decided", BudgetScreen::decided}})},
        {"config", config_json(cfg)},
        {"tool_calls", tool_calls_json()},
        {"n_constant_folded", SatisfiabilityChecker::n_constant_folded.load()},
        {"n_spot_decided", SatisfiabilityChecker::n_spot_decided.load()},
        {"n_escalations_declined",
         SatisfiabilityChecker::n_escalations_declined.load()},
        {"n_weak_operator_unresolved",
         SatisfiabilityChecker::n_weak_operator_unresolved.load()},
        {"n_tautology_substitutions", Ltl2tgbaStats::n_tautology_substitutions},
        // Repairs the cross-generation accumulator added that the final
        // population's own collection did not already hold. Zero when
        // genetic.accumulate_repairs is off, and also when every accumulated
        // repair happened to survive to the last generation, so it is read
        // against the config key rather than on its own.
        {"n_accumulated_repairs", AccumulatorStats::n_contributed},
        // ltlsynt calls that reported SPOT's acceptance-set ceiling rather than
        // a verdict, resolved as undecided rather than ending the run.
        {"n_ltlsynt_capability_errors",
         RealizabilityChecker::n_capability_errors},
        // Well-separation queries that raised instead of answering, resolved as
        // undecided (the candidate is dropped) rather than propagating.
        {"n_well_separation_errors", WellSeparationStats::n_errors.load()},
        {"fitness_cache", fitness_cache_json()},
        {"caches", caches_json()},
        {"implication", implication_json()}};

    const std::filesystem::path path = dir / k_run_manifest_name;
    std::ofstream out(path);
    if (!out) {
        std::cerr << "warning: could not write " << path.string()
                  << "; the run's repairs are unaffected\n";
        return;
    }
    out << manifest.dump(2) << "\n";
}
