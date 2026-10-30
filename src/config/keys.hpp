#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <variant>
#include <vector>

#include "config.hpp"

// What the parser demands of a key's value.
enum class KeyCheck : std::uint8_t {
    None,
    Probability,  // in [0, 1]
    NonNegative,  // >= 0
    Positive,     // > 0, which for an integer is >= 1
};

// Which repair path reads a key. A run warns about a key only the other path
// reads when it is set away from its default, and records it as null.
enum class KeyReader : std::uint8_t { Both, Fretish, Tlsf };

using ConfigMember =
    std::variant<double Config::*, std::size_t Config::*, bool Config::*,
                 std::chrono::milliseconds Config::*, SelectionScheme Config::*,
                 TerminationMode Config::*, StatusGrading Config::*,
                 MrsAdmissionOrder Config::*>;

struct ConfigKey {
    const char* section;  // dotted, as in "tlsf.mutation"
    const char* key;
    ConfigMember member;
    KeyCheck check;
    KeyReader reader;
    const char* description;  // for schemas/config-schema.json
    const char* note;         // comment lines above it in example-config.toml
};

// Every TOML key, which the parser reads (src/config_io.cpp), declares to its
// unknown-key warning, and writes back into run.json (src/repair/manifest.cpp).
// schemas/config-schema.json and example-config.toml are generated from it
// (src/config/reference.cpp). Listed in read order, which decides the error
// reported for a config with more than one.
inline constexpr std::array<ConfigKey, 38> k_config_keys{{
    {"genetic", "generations", &Config::generations, KeyCheck::Positive,
     KeyReader::Both, "Number of evolution rounds.", ""},
    {"genetic", "population_size", &Config::population_size, KeyCheck::Positive,
     KeyReader::Both, "Candidates per generation.", ""},
    {"genetic", "selection_rate", &Config::selection_rate,
     KeyCheck::Probability, KeyReader::Both,
     "Fraction of population selected as parents each generation.", ""},
    {"genetic", "elitism_rate", &Config::elitism_rate, KeyCheck::Probability,
     KeyReader::Both,
     "Fraction of the population carried over verbatim each generation, "
     "bypassing crossover, mutation, and filters. Must be less than "
     "selection_rate. Redundant on paper under either NSGA-II scheme, whose "
     "(mu+lambda) survivor step is already elitist, and kept at 0.1 for wall "
     "time alone: the only measured difference is that 0 costs 4.0% more, "
     "yield and quality being level.",
     "elites are a subset of the parents, so this must stay < selection_rate.\n"
     "Redundant on paper under NSGA-II, and kept at 0.1 for wall time\n"
     "alone: the 2026-08-23 monotone campaign puts 0 at 4.0% more wall\n"
     "time and level on yield and quality -- see docs/configuration.rst.\n"},
    {"genetic", "crossover_rate", &Config::crossover_rate,
     KeyCheck::Probability, KeyReader::Both,
     "Probability of crossover between parents.", ""},
    {"genetic", "mutation_rate", &Config::mutation_rate, KeyCheck::Probability,
     KeyReader::Both, "Probability of mutating an offspring.",
     "per offspring, unlike the per-requirement rates in [mutation]\n"},
    {"genetic", "max_individuals", &Config::max_individuals,
     KeyCheck::NonNegative, KeyReader::Both,
     "Offspring budget for the whole run under termination = 'individuals'; "
     "ignored under 'generations'. An offspring counts only when it differs "
     "from the parent it was bred from, matching what AuRUS counts. Zero is "
     "rejected under 'individuals' rather than meaning unlimited.",
     "Offspring budget under termination = \"individuals\"; ignored otherwise, "
     "and\n"
     "rejected at 0 there rather than read as unlimited.\n"},
    {"genetic", "max_wall_s", &Config::max_wall_s, KeyCheck::NonNegative,
     KeyReader::Both,
     "Wall-clock deadline for the whole run in seconds; 0 is no deadline. "
     "Independent of termination and honoured under both. The search stops at "
     "the first generation boundary or bred offspring past the deadline and "
     "then writes its output normally, which is why this beats the harness's "
     "external kill: a killed run leaves no run.json and is censored with "
     "nothing recorded, where a run that stops itself reports what it found "
     "and which budget ended it. Measured from the same origin as the "
     "manifest's wall_s. Filters, scoring and the final realizability gate are "
     "not interrupted, so a run overruns by whatever the generation it was in "
     "had left.",
     "Wall-clock deadline for the whole run in seconds, 0 for none. Honoured "
     "under\n"
     "both termination modes. A run that stops on its own deadline still "
     "writes\n"
     "run.json and its repairs, where one killed by the harness writes "
     "neither.\n"},
    {"genetic", "termination", &Config::termination, KeyCheck::None,
     KeyReader::Both,
     "Which budget ends the search. 'generations' is PEREDUR's own: the "
     "[genetic] generations rounds, whatever they cost. 'individuals' is "
     "AuRUS's, for a head-to-head where both tools get the same number of "
     "offspring rather than the same wall clock -- its GA stops once "
     "numberOfVisitedIndividuals reaches GA_MAX_NUM_INDIVIDUALS, counted per "
     "offspring admitted to the new population and checked between offspring "
     "rather than at a generation boundary. generations still bounds the run "
     "under 'individuals', as GA_GENERATIONS does for AuRUS, and max_wall_s is "
     "honoured under both. Requires max_individuals >= 1.",
     "What ends the search. \"generations\" is PEREDUR's own budget: the "
     "rounds above,\n"
     "whatever they cost. \"individuals\" is AuRUS's, for a head-to-head where "
     "both\n"
     "tools get the same number of offspring rather than the same wall clock; "
     "it\n"
     "stops once max_individuals offspring have been bred, counted per "
     "offspring\n"
     "that differs from its parent and checked between offspring rather than "
     "at a\n"
     "generation boundary. generations still bounds the run under either.\n"},
    {"genetic", "selection_scheme", &Config::selection_scheme, KeyCheck::None,
     KeyReader::Both,
     "Selection scheme: 'nsga2-apportion' (default) ranks the (mu + lambda) "
     "pool by Pareto non-domination and crowding distance over the individual "
     "fitness components, deduplicating the pool first and apportioning the "
     "population_size slots over the distinct survivors by 1 / (1 + rank) "
     "under the largest-remainder (Hamilton) method; 'weighted' ranks by the "
     "weighted-average fitness scalar, but converges prematurely and is "
     "retained for comparison. Spelled 'nsga2' and 'nsga2-replicate' before "
     "2026-08-06, and 'nsga2-truncate' was removed on 2026-10-30; those values "
     "are rejected, so reproduce an archived campaign at the commit its "
     "PROVENANCE.json names.",
     "\"nsga2-apportion\" | \"weighted\". Under NSGA-II\n"
     "the [fitness] weights only select which components are active (weight > "
     "0);\n"
     "they no longer bias selection, and the (mu+lambda) survivor pooling is "
     "already\n"
     "elitist, so elitism_rate = 0 is the natural companion on paper -- though "
     "it\n"
     "measured slower in both campaigns that tested it, which is why the "
     "default\n"
     "stays at 0.1.\n"
     "\"nsga2-truncate\", the default until 2026-08-14, was removed on "
     "2026-10-30.\n"},
    {"fitness", "weight_syntactic", &Config::fitness_weight_syntactic,
     KeyCheck::NonNegative, KeyReader::Both,
     "Syntactic similarity component weight. The defaults match AuRUS's "
     "Settings.java: 0.1 syntactic, 0.2 semantic (its lost and won models at "
     "0.1 each) and 0.7 status.",
     "AuRUS's weights (Settings.java): its lost- and won-model terms, 0.1 "
     "each, are\n"
     "the one semantic term here.\n"},
    {"fitness", "weight_semantic", &Config::fitness_weight_semantic,
     KeyCheck::NonNegative, KeyReader::Both,
     "Semantic similarity (model counting) component weight.", ""},
    {"fitness", "weight_status", &Config::fitness_weight_status,
     KeyCheck::NonNegative, KeyReader::Both, "Realizability status weight.",
     ""},
    {"fitness", "status_grading", &Config::status_grading, KeyCheck::None,
     KeyReader::Both,
     "How the status objective grades below realizability: 'mrs' (default) "
     "scores 0 for an unsatisfiable component or specification, and otherwise "
     "the greedy maximal-realizable-subset fraction, admitting guarantee-side "
     "parts in the order mrs_admission_order sets and scoring the fraction "
     "kept. The three-point 'tiered' scale was removed on 2026-10-30, after "
     "the 2026-08-11 campaign measured mrs at 410/480 repairs against its "
     "367/480. 'aurus' is AuRUS's own six-level ladder (0, 0.05, 0.1, 0.2, "
     "0.5, 1.0), graded by which side of the specification is satisfiable on "
     "its own; it is the only scale that does not fold well-separation into "
     "its realizability query, because AuRUS never asks that question, and it "
     "exists for an ablation against the design peredur derives from.",
     "\"mrs\" grades the unrealizable region by the fraction of guarantee-side "
     "parts a\n"
     "greedy sweep keeps realizable.\n"
     "\"aurus\" is AuRUS's own ladder, graded by which side is satisfiable "
     "alone, and\n"
     "is the one scale that does not penalise a candidate realizable only by\n"
     "defeating its own assumptions -- it is there to be ablated against, not "
     "used.\n"
     "or \"aurus\"\n"},
    {"fitness", "mrs_admission_order", &Config::mrs_admission_order,
     KeyCheck::None, KeyReader::Both,
     "Which order the MRS walk admits guarantee-side parts in, read only under "
     "status_grading = 'mrs'. 'spec' is the specification's own index order. "
     "'degree' (default) admits parts by ascending pairwise-conflict degree, "
     "so a part that cannot be held with most of the others is deferred rather "
     "than blocking them; it costs n(n-1)/2 + n subset queries once, before "
     "the search starts. Both are constant across a run, which is what the "
     "walk needs for seed reproducibility and for RealizabilityChecker's "
     "cache. Measured over populations of mutants across six TLSF "
     "specifications, degree scores 0.587 against spec order's 0.529 at 1.02x "
     "the ltlsynt execs.",
     "Which order that sweep admits parts in. \"degree\" defers the parts "
     "that\n"
     "conflict with most others, costing one pairwise pass over the input "
     "before the\n"
     "search starts; \"spec\" is the specification's own index order.\n"
     "or \"spec\"\n"},
    {"mutation", "p_trigger", &Config::p_trigger, KeyCheck::Probability,
     KeyReader::Fretish, "Per-requirement probability of mutating the trigger.",
     "Per-requirement probabilities of rewriting each part of a FRETISH "
     "requirement.\n"},
    {"mutation", "p_response", &Config::p_response, KeyCheck::Probability,
     KeyReader::Fretish,
     "Per-requirement probability of mutating the response.", ""},
    {"mutation", "p_timing", &Config::p_timing, KeyCheck::Probability,
     KeyReader::Fretish, "Per-requirement probability of mutating the timing.",
     ""},
    {"mutation", "p_condition_type", &Config::p_condition_type,
     KeyCheck::Probability, KeyReader::Fretish,
     "FRETISH only: per-requirement probability of moving the condition type "
     "along its implication order (Continual strengthens, Trigger weakens).",
     "FRETISH only, at p_timing's rate. p_scope needs the specification to "
     "declare a\n"
     "mode to have anywhere to go. Set either to 0 to reproduce a run from "
     "before\n"
     "2026-09-11, when both defaulted off.\n"},
    {"mutation", "p_scope", &Config::p_scope, KeyCheck::Probability,
     KeyReader::Fretish,
     "FRETISH only: per-requirement probability of moving the scope along its "
     "implication order. Needs a declared mode to reach any non-Global scope.",
     ""},
    {"mutation", "p_stop", &Config::p_stop, KeyCheck::Probability,
     KeyReader::Fretish,
     "FRETISH only: per-requirement probability of rewriting the stop "
     "condition of an until or before timing. Draws nothing on a requirement "
     "without one.",
     "FRETISH only: rewrites the stop condition of an `until` or `before` "
     "timing,\n"
     "and draws nothing on a requirement without one.\n"},
    {"mutation", "p_monotone", &Config::p_monotone, KeyCheck::Probability,
     KeyReader::Both,
     "Both modes: probability that a rewrite takes the monotone form, whose "
     "result is comparable to what it replaces under implication. On the "
     "FRETISH path it is offered inside the condition and response rewrites "
     "and takes the requirement's own direction; on the TLSF path it is "
     "offered ahead of the temporal and propositional rewrites, its direction "
     "a fair coin.",
     "Both modes: the share of rewrites that take the monotone form, moving a\n"
     "formula along the implication order rather than rewriting it freely. On "
     "the\n"
     "FRETISH path it is offered inside the condition and response rewrites, "
     "on the\n"
     "TLSF path ahead of the temporal and propositional ones.\n"},
    {"mutation", "p_add_assumption", &Config::p_add_assumption,
     KeyCheck::Probability, KeyReader::Both,
     "Both modes: probability a mutation appends a new environment assumption "
     "(fairness over an input) instead of rewriting an existing "
     "requirement/formula.",
     "Both modes: the probability that a mutation instead appends a new "
     "environment\n"
     "assumption, and the fraction of those guarded by a random input atom "
     "rather\n"
     "than by `true`.\n"},
    {"mutation", "p_remove_guarantee", &Config::p_remove_guarantee,
     KeyCheck::Probability, KeyReader::Both,
     "Both modes: probability a mutation deletes one guarantee instead of "
     "rewriting a requirement/formula. The mirror of p_add_assumption; never "
     "removes the last live guarantee. Set to 0 to reproduce a run from before "
     "the operator existed.",
     "The mirror action: delete a guarantee outright, never the last live "
     "one.\n"
     "Some repairs are unreachable without it. Set to 0 to reproduce a run "
     "from\n"
     "before the operator existed.\n"},
    {"mutation", "p_conditional_assumption", &Config::p_conditional_assumption,
     KeyCheck::Probability, KeyReader::Both,
     "Of the appended assumptions, the fraction guarded by a random input atom "
     "rather than by 'true'.",
     ""},
    {"model_counting", "default_bound", &Config::default_model_counting_bound,
     KeyCheck::Positive, KeyReader::Both,
     "Trace-length bound k for bounded model counting.", ""},
    {"filters", "run_implication", &Config::run_implication_filter,
     KeyCheck::None, KeyReader::Both,
     "Enable final implication maximality filter.", ""},
    {"runtime", "black_timeout_ms", &Config::black_timeout,
     KeyCheck::NonNegative, KeyReader::Both,
     "Timeout per black SAT query in milliseconds.",
     "Per-call wall-clock budgets in ms; 0 disables one. Ganak takes none:\n"
     "counting is the fitness function's real work, so a slow count is usually "
     "a\n"
     "legitimately hard one rather than a blowup. The rest are bounded because "
     "an\n"
     "unbounded call can stall a whole run.\n"},
    {"runtime", "ltlsynt_timeout_ms", &Config::ltlsynt_timeout,
     KeyCheck::NonNegative, KeyReader::Both,
     "Per-call ltlsynt realizability timeout in milliseconds. Default 10000; 0 "
     "is no timeout. A call exceeding it is killed and reported as undecided, "
     "which does not admit a repair and drops the candidate at the "
     "well-separation filter. It was 500, on the measurement that call "
     "durations are bimodal (95% finish under 50 ms, and a 500 ms cap abandons "
     "within 0.1% of what a 10 s cap does) -- a per-corpus statistic that "
     "hides the per-specification failure: where a specification's own "
     "realizability query exceeds the budget it loses not 0.1% of its calls "
     "but all of them, and even a correct repair is rejected at the final "
     "gate. The startup budget screen warns when the input specification is in "
     "that position.",
     ""},
    {"runtime", "ltl2tgba_timeout_ms", &Config::ltl2tgba_timeout,
     KeyCheck::NonNegative, KeyReader::Both,
     "Per-call ltl2tgba automaton-construction timeout in milliseconds. "
     "Default 60000; 0 is no timeout. Looser than the ltlsynt budget because a "
     "legitimate determinisation can take tens of seconds. A call exceeding it "
     "drops the individual against max_scoring_failure_rate.",
     ""},
    {"runtime", "ltlfilt_timeout_ms", &Config::ltlfilt_timeout,
     KeyCheck::NonNegative, KeyReader::Both,
     "Per-call ltlfilt simplification timeout in milliseconds. Default 10000; "
     "0 is no timeout. An abandoned call returns the formula unsimplified "
     "rather than failing.",
     ""},
    {"runtime", "parallel", &Config::parallel, KeyCheck::Positive,
     KeyReader::Both,
     "Thread pool size. Omit to use the hardware concurrency, narrowed by the "
     "CPU affinity mask and the cgroup CPU quota.",
     "Thread pool size; the default is the hardware concurrency narrowed by\n"
     "the CPU affinity mask and the cgroup CPU quota, so this key has no "
     "fixed\n"
     "default to pin and stays commented out.\n"},
    {"runtime", "mrs_speculation_depth", &Config::mrs_speculation_depth,
     KeyCheck::NonNegative, KeyReader::Both,
     "How many steps ahead the MRS walk queries both outcomes of the steps it "
     "has not yet settled, on pool workers that would otherwise sit idle; read "
     "only under status_grading = 'mrs'. 0 turns it off. The walk asks the "
     "same queries in the same order either way, so scores are unchanged; "
     "only wall time and the ltlsynt execs spent on untaken branches move. "
     "Depth d keeps up to 2^(d+1) - 2 queries in flight beside the walk's "
     "own. Values above 8 read as 8.",
     "How many steps ahead the MRS walk queries both outcomes into idle "
     "workers.\n"
     "Scores are unchanged at any value; 0 turns it off.\n"},
    {"runtime", "dashboard", &Config::dashboard, KeyCheck::None,
     KeyReader::Both,
     "Stream progress to <output-dir>/progress.jsonl and copy the live "
     "dashboard page beside it. Off by default; --dashboard also enables it.",
     ""},
    {"runtime", "max_scoring_failure_rate", &Config::max_scoring_failure_rate,
     KeyCheck::Probability, KeyReader::Both,
     "Fraction of a generation whose fitness scoring may throw before the run "
     "aborts. A single failure per generation is always tolerated. Default "
     "0.15, because the ltl2tgba budget drops individuals by design and enough "
     "land in one generation on the heavy TLSF specifications to trip a "
     "tighter bound.",
     "0.15, not 0.05: the ltl2tgba budget drops individuals by design, and "
     "enough\n"
     "land in one generation on the heavy TLSF specs to trip a tighter "
     "bound.\n"},
    {"tlsf.mutation", "p_assumption", &Config::tlsf_p_assumption,
     KeyCheck::Probability, KeyReader::Tlsf,
     "Probability of mutating an assumption-side section "
     "(INITIALLY/REQUIRE/ASSUME); the guarantee side (PRESET/ASSERT/GUARANTEE) "
     "takes the complement.",
     ""},
    {"tlsf.mutation", "p_temporal", &Config::tlsf_p_temporal,
     KeyCheck::Probability, KeyReader::Tlsf,
     "Probability that a chosen section formula is rewritten by the "
     "temporal-structure mutation (may insert/drop/swap X/F/G/U/R/W) rather "
     "than the skeleton-preserving propositional rewrite.",
     ""},
    {"tlsf.mutation", "p_clone_assumption", &Config::tlsf_p_clone_assumption,
     KeyCheck::Probability, KeyReader::Tlsf,
     "Of the assumptions p_add_assumption appends, the fraction that copy an "
     "existing live ASSUME conjunct rather than build one from the template; "
     "ordinary mutation then edits the copy. Falls back to the template when "
     "there is nothing live to copy.",
     "Of the assumptions [mutation] p_add_assumption appends, the fraction "
     "that copy\n"
     "an existing one instead of building from the template, which emits at "
     "most\n"
     "seven nodes where the assumption-shaped ideals run to thirty.\n"},
}};

// The table names along a dotted section, outermost first.
inline std::vector<std::string_view> section_path(std::string_view dotted) {
    std::vector<std::string_view> names;
    for (;;) {
        const std::size_t dot = dotted.find('.');
        names.push_back(dotted.substr(0, dot));
        if (dot == std::string_view::npos) {
            return names;
        }
        dotted.remove_prefix(dot + 1);
    }
}
