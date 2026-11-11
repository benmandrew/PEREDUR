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
#include <vector>

#include <nlohmann/json.hpp>

#include "config/enum_names.hpp"
#include "config/keys.hpp"
#include "filter/correctness.hpp"
#include "filter/implication.hpp"
#include "filter/well_separation.hpp"
#include "fitness/function.hpp"
#include "fitness/semantic_similarity.hpp"
#include "fitness/status.hpp"
#include "genetic/accumulator.hpp"
#include "genetic/climb.hpp"
#include "genetic/demotion.hpp"
#include "genetic/mutation.hpp"
#include "genetic/order_crossover.hpp"
#include "genetic/output_gate.hpp"
#include "genetic/pipeline.hpp"
#include "runner/black.hpp"
#include "runner/ganak.hpp"
#include "runner/ltlfilt.hpp"
#include "runner/process.hpp"
#include "runner/spot.hpp"
#include "runner/tool_paths.hpp"
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
// shape it is holding. Each version is described under "Run manifest schema
// versions" in docs/dev/config-and-provenance.md.
constexpr int k_schema_version = 65;

// True for a key only the other repair path reads.
bool unread_on(const ConfigKey& entry, RepairInput input) {
    switch (entry.reader) {
        case KeyReader::Both:
            return false;
        case KeyReader::Fretish:
            return input != RepairInput::Fretish;
        case KeyReader::Tlsf:
            return input != RepairInput::Tlsf;
    }
    return false;
}

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

}  // namespace

nlohmann::json::json_pointer config_pointer(const ConfigKey& entry) {
    std::string pointer;
    for (const std::string_view name : section_path(entry.section)) {
        pointer += "/" + std::string(name);
    }
    return nlohmann::json::json_pointer(pointer + "/" + entry.key);
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

namespace {

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
        // Every ltlfilt exec, over both counted entry points, which is the
        // set total_s is over. Its per-entry-point split is in `caches`.
        {"ltlfilt", row(LtlfiltStats::n_execs(), LtlfiltStats::n_remove_wm_hits,
                        LtlfiltStats::n_timeouts, LtlfiltStats::total_time_s)},
        {"ltlsynt", row(RealizabilityChecker::n_cache_misses,
                        RealizabilityChecker::n_cache_hits,
                        RealizabilityChecker::n_timeouts,
                        RealizabilityChecker::total_time_s)},
        // The controller store's ltl2tgba and autfilt execs, apart from the
        // counting path's ltl2tgba row.
        {"controller_check", row(ControllerReuseStats::n_execs,
                                 ControllerReuseStats::n_verdict_hits,
                                 ControllerReuseStats::n_timeouts,
                                 ControllerReuseStats::total_time_s)},
        // "calls" is black's own exec count: one per SPOT SAT it confirms,
        // per `ExpectSat` query SPOT left undecided, and per model probe.
        {"black", row(SatisfiabilityChecker::n_black_calls,
                      SatisfiabilityChecker::n_cache_hits,
                      SatisfiabilityChecker::n_timeouts,
                      SatisfiabilityChecker::total_time_s)},
        // The ltl2tgba and autfilt execs that confirm a SPOT SAT black left
        // undecided; cache_hits is always 0, as nothing memoises them here.
        {"spot_emptiness",
         row(EmptinessStats::n_execs, 0, EmptinessStats::n_timeouts,
             EmptinessStats::total_time_s)},
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
// `model_words` counts the solver models among those words, summed over the
// tables a streamed sweep draws per batch, and `model_refuted` the ordered
// pairs within `fingerprint_refuted` that only a model word refuted.
// `residual_queries` and `residual_settled` count ordered checks too: those
// that dropped shared guarantees before querying, and those left nothing to
// query. `rule_covered` counts requirements, not checks.
nlohmann::json implication_json() {
    return {
        {"comparisons", ImplicationFilterStats::n_comparisons.load()},
        {"skipped", ImplicationFilterStats::n_skipped.load()},
        {"duplicates", ImplicationFilterStats::n_duplicates.load()},
        {"timeouts", ImplicationFilterStats::n_timeouts.load()},
        {"equivalent_collapsed",
         ImplicationFilterStats::n_equivalent_collapsed.load()},
        {"fingerprint_refuted",
         ImplicationFilterStats::n_fingerprint_refuted.load()},
        {"model_words", ImplicationFilterStats::n_model_words.load()},
        {"model_refuted", ImplicationFilterStats::n_model_refuted.load()},
        {"residual_queries", ImplicationFilterStats::n_residual_queries.load()},
        {"residual_settled", ImplicationFilterStats::n_residual_settled.load()},
        {"rule_covered", ImplicationFilterStats::n_rule_covered.load()}};
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
         nlohmann::json{
             {"hits", RealizabilityChecker::n_cache_hits},
             {"misses", RealizabilityChecker::n_cache_misses},
             // Answered from the monotone subsumption table rather
             // than by an exec. Not a hit: a hit is the same
             // question asked twice.
             {"subsumed", RealizabilityChecker::n_subsumed},
             {"rule_subsumed", RealizabilityChecker::n_rule_subsumed},
             // Decided by satisfiability, the query having no inputs.
             {"satisfiability_routed",
              RealizabilityChecker::n_satisfiability_routed},
             // Answered realizable by a controller ltlsynt emitted for
             // another query; disjoint from the counts above.
             {"controller_reuse",
              {{"attempts", ControllerReuseStats::n_attempts},
               {"hits", ControllerReuseStats::n_hits},
               {"guarantees_checked",
                ControllerReuseStats::n_guarantees_checked},
               {"verdicts_recalled", ControllerReuseStats::n_verdict_hits},
               {"stored", ControllerReuseStats::n_stored},
               // Turned away by the screen-time gate.
               {"gated", ControllerReuseStats::n_gated}}},
             // The execs (misses) split by outcome, with the speculative
             // ones and the slow decided ones counted again within them.
             {"execs",
              {{"realizable", RealizabilityChecker::n_exec_realizable},
               {"unrealizable", RealizabilityChecker::n_exec_unrealizable},
               {"timed_out", RealizabilityChecker::n_timeouts},
               {"capability_errors", RealizabilityChecker::n_capability_errors},
               {"speculative", RealizabilityChecker::n_speculative_execs},
               // Queries asked again without decomposition, a second exec
               // each, and those the second exec decided.
               {"decompose_reasks", RealizabilityChecker::n_decompose_reasks},
               {"reasks_decided", RealizabilityChecker::n_reasks_decided},
               {"decided_over_1s", RealizabilityChecker::n_decided_over_1s},
               {"decided_over_2s", RealizabilityChecker::n_decided_over_2s},
               {"decided_over_5s", RealizabilityChecker::n_decided_over_5s},
               {"slowest_decided_s",
                RealizabilityChecker::slowest_decided_s}}}}},
        {"count_traces",
         row(CountTracesStats::n_hits, CountTracesStats::n_misses)},
        {"ltl2tgba",
         row(Ltl2tgbaStats::n_cache_hits, Ltl2tgbaStats::n_cache_misses)},
        {"ganak", row(GanakStats::n_cache_hits, GanakStats::n_cache_misses)},
        {"remove_wm",
         row(LtlfiltStats::n_remove_wm_hits, LtlfiltStats::n_remove_wm_execs)},
        // Memoised nowhere: check_satisfiability caches the decided answer one
        // layer up, so a repeat never reaches it. Reported with no hits rather
        // than omitted, so that a reader summing execs finds it.
        {"spot_satisfiable", row(0, LtlfiltStats::n_satisfiable_execs)}};
}

// The external tools as this process resolved them. Two hosts at one commit
// can run different solvers, and an environment override can swap one in
// without a rebuild, so `commit` alone does not say what gave the verdicts.
// Spot's `path` is its bin directory, since every Spot tool is read from it.
// The formaliser is absent: it is a test oracle, and no binary runs it.
nlohmann::json external_tools_json() {
    auto row = [](const ToolPath& path, const ToolVersion& version) {
        return nlohmann::json{
            {"path", path.m_path},
            {"from_env", path.m_from_env},
            {"version", version.m_version ? nlohmann::json(*version.m_version)
                                          : nlohmann::json(nullptr)},
            {"version_error", version.m_error.empty()
                                  ? nlohmann::json(nullptr)
                                  : nlohmann::json(version.m_error)}};
    };
    // The server answers with the code of the Spot it was linked from, which
    // the build ties to the `spot` row's version; `active` says whether the
    // run's Spot calls went through it, which decides whether its controllers
    // can be compared byte for byte with an exec run's.
    const ToolPath& server = spot_server_path_source();
    return {{"spot", row(spot_bin_dir_source(), spot_version())},
            {"spot_server",
             {{"path", server.m_path.empty() ? nlohmann::json(nullptr)
                                             : nlohmann::json(server.m_path)},
              {"from_env", server.m_from_env},
              {"active", tool_server_active()}}},
            {"ganak", row(ganak_path_source(), ganak_version())},
            {"black", row(black_path_source(), black_version())}};
}

nlohmann::json fitness_cache_json() {
    return {{"hits", FitnessCacheStats::n_hits},
            {"misses", FitnessCacheStats::n_misses}};
}

nlohmann::json order_crossover_json() {
    constexpr std::array<const char*, k_order_crossover_kinds> k_names = {
        "join", "meet", "relink"};
    nlohmann::json counts = nlohmann::json::object();
    for (std::size_t kind = 0; kind < k_order_crossover_kinds; ++kind) {
        counts[k_names[kind]] = {
            {"attempted", OrderCrossoverStats::n_attempted[kind].load()},
            {"proved", OrderCrossoverStats::n_proved[kind].load()},
            {"realizable", OrderCrossoverStats::n_realizable[kind].load()}};
    }
    return counts;
}

}  // namespace

std::vector<std::string> ignored_non_default_keys(const Config& cfg,
                                                  RepairInput input) {
    // Compared through config_json() so that each key is read by the same code
    // that records it, and a key added to one table cannot drift from the
    // other.
    const nlohmann::json actual = config_json(cfg);
    const nlohmann::json defaults = config_json(Config{});
    std::vector<std::string> ignored;
    for (const ConfigKey& entry : k_config_keys) {
        if (unread_on(entry, input) && actual.at(config_pointer(entry)) !=
                                           defaults.at(config_pointer(entry))) {
            ignored.push_back(std::string(entry.section) + "." + entry.key);
        }
    }
    return ignored;
}

void write_run_manifest(const std::string& output_dir,
                        const std::string& input_path, std::size_t seed,
                        const Config& cfg, double wall_s,
                        const SearchBudget& budget, RepairInput input) {
    const std::filesystem::path dir(output_dir);
    nlohmann::json config = config_json(cfg);
    for (const ConfigKey& entry : k_config_keys) {
        if (unread_on(entry, input)) {
            config[config_pointer(entry)] = nullptr;
        }
    }
    const nlohmann::json manifest{
        {"schema_version", k_schema_version},
        {"tool", "peredur"},
        {"commit", version::commit()},
        {"commit_short", version::commit_short()},
        {"dirty", version::dirty()},
        {"external_tools", external_tools_json()},
        {"finished_utc", utc_timestamp()},
        {"wall_s", wall_s},
        // Of wall_s, the seconds the post-search tail took: the output gate on
        // the final population, the climb, the final filters and the rescore.
        // Machine-dependent, as wall_s is.
        {"finish_wall_s", OutputTailStats::wall_s},
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
        {"config", std::move(config)},
        {"tool_calls", tool_calls_json()},
        // All zero unless runtime.spot_server is on. A failure is a call that
        // fell back to an exec. At a timeout or cancellation, `killed` counts
        // tools killed by a server that was kept, and `discarded` servers
        // killed with their tool.
        {"spot_server",
         {{"started", ToolServerStats::n_started.load()},
          {"requests", ToolServerStats::n_requests.load()},
          {"killed", ToolServerStats::n_killed.load()},
          {"discarded", ToolServerStats::n_discarded.load()},
          {"failures", ToolServerStats::n_failures.load()}}},
        {"n_constant_folded", SatisfiabilityChecker::n_constant_folded.load()},
        {"n_spot_decided", SatisfiabilityChecker::n_spot_decided.load()},
        {"n_escalations_declined",
         SatisfiabilityChecker::n_escalations_declined.load()},
        {"n_model_probe_sat", SatisfiabilityChecker::n_model_probe_sat.load()},
        {"n_spot_sat_refuted",
         SatisfiabilityChecker::n_spot_sat_refuted.load()},
        {"n_spot_sat_unconfirmed",
         SatisfiabilityChecker::n_spot_sat_unconfirmed.load()},
        {"n_weak_operator_unresolved",
         SatisfiabilityChecker::n_weak_operator_unresolved.load()},
        {"n_tautology_substitutions", Ltl2tgbaStats::n_tautology_substitutions},
        {"mrs_screen",
         {{"screened", MrsScreenStats::n_screened.load()},
          {"unsatisfiable", MrsScreenStats::n_unsatisfiable.load()},
          {"undecided", MrsScreenStats::n_undecided.load()}}},
        {"mrs_speculation",
         {{"issued", MrsSpeculationStats::n_issued.load()},
          {"started", MrsSpeculationStats::n_started.load()},
          {"consumed", MrsSpeculationStats::n_consumed.load()},
          {"killed", MrsSpeculationStats::n_killed.load()}}},
        // Zero on every count unless genetic.p_order_crossover is above 0;
        // TLSF runs never take the arm.
        {"order_crossover", order_crossover_json()},
        // Repairs the cross-generation accumulator added that the final
        // population's own collection did not already hold. Zero when every
        // accumulated repair happened to survive to the last generation.
        {"n_accumulated_repairs", AccumulatorStats::n_contributed},
        // Rewrites whose slot blame_targeted_slots drew from a narrowed list.
        // Zero whenever the key is off.
        {"n_targeted_mutations", SlotTargetingStats::n_targeted.load()},
        // Every FRETISH mutation, and those that reached the slot draw, which
        // n_targeted_mutations is a subset of. Zero on the TLSF path.
        {"n_mutations", MutationStats::n_mutations.load()},
        {"n_slot_draws", MutationStats::n_slot_draws.load()},
        // Candidates maximality demotion moved one front down at survivor
        // selection, over every generation. Zero wherever the key is off.
        {"n_maximality_demotions", DemotionStats::n_demoted},
        // The final population's first front by objectives alone, and its
        // realizable members another realizable member is proved strictly
        // stronger than. Measured whether or not demotion is on; null without
        // a status objective and on the TLSF path.
        {"final_front",
         DemotionStats::final_front.has_value()
             ? nlohmann::json(
                   {{"members", DemotionStats::final_front->n_members},
                    {"realizable", DemotionStats::final_front->n_realizable},
                    {"gutted", DemotionStats::final_front->n_gutted}})
             : nlohmann::json(nullptr)},
        // The output climb, all zero unless filters.climb_output is on.
        {"climb",
         {{"climbed", ClimbStats::n_climbed.load()},
          {"steps_tried", ClimbStats::n_steps_tried.load()},
          {"steps_accepted", ClimbStats::n_steps_accepted.load()},
          {"queries", ClimbStats::n_queries.load()},
          {"ltlsynt_execs", ClimbStats::n_ltlsynt_execs.load()},
          {"memo_hits", ClimbStats::n_memo_hits.load()},
          {"subsumed", ClimbStats::n_subsumed.load()},
          {"rule_subsumed", ClimbStats::n_rule_subsumed.load()},
          {"satisfiability_routed", ClimbStats::n_satisfiability_routed.load()},
          {"controller_hits", ClimbStats::n_controller_hits.load()},
          {"wall_s", ClimbStats::wall_s}}},
        // What direction_gated did: free rewrites returned as the parent for
        // moving the forbidden way or for a refused sideways draw, and steps
        // a realizable parent took back towards the original. Zero whenever
        // the key is off.
        {"direction_gate",
         {{"forbidden", DirectionGateStats::n_forbidden.load()},
          {"sideways_refused", DirectionGateStats::n_sideways_refused.load()},
          {"steps", DirectionGateStats::n_steps.load()},
          {"bisected", DirectionGateStats::n_bisected.load()},
          {"steps_realizable", DirectionGateStats::n_steps_realizable.load()}}},
        // What step_scaling did: mutations run under a scale, and the large
        // moves and single tick-count edges they took, zero whenever the key
        // is off. The unscaled counts are the same three for the mutations a
        // scale would have applied to with the key at 0, zero whenever it is
        // on.
        {"step_scaling",
         {{"scaled", StepScalingStats::n_scaled.load()},
          {"large", StepScalingStats::n_large.load()},
          {"small", StepScalingStats::n_small.load()},
          {"unscaled", StepScalingStats::n_unscaled.load()},
          {"unscaled_large", StepScalingStats::n_unscaled_large.load()},
          {"unscaled_small", StepScalingStats::n_unscaled_small.load()}}},
        // What the parent-verdict stage did: offspring dropped as proved
        // strengthenings of a parent the realizability memo showed
        // unrealizable, and proved strengthenings kept because the parent's
        // verdict was undecided. Zero whenever the key is off.
        {"parent_verdict",
         {{"dropped", ParentVerdictStats::n_dropped.load()},
          {"unproved", ParentVerdictStats::n_unproved.load()}}},
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
