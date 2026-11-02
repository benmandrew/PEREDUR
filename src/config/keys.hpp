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
                 MrsAdmissionOrder Config::*, ClimbOrder Config::*>;

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
inline constexpr std::array<ConfigKey, 52> k_config_keys{{
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
    {"genetic", "p_order_crossover", &Config::p_order_crossover,
     KeyCheck::Probability, KeyReader::Fretish,
     "FRETISH only: of the crossovers whose parents both carry a "
     "realizability verdict, the fraction that take the order crossover: a "
     "join of two realizable parents, a meet of two unrealizable ones, or path "
     "relinking between one of each. 0 draws nothing.",
     "FRETISH only: the share of crossovers between parents with known\n"
     "realizability that join, meet or relink them along the implication "
     "order\n"
     "instead of grafting subformulae. 0 draws nothing.\n"},
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
    {"genetic", "maximality_demotion", &Config::maximality_demotion,
     KeyCheck::None, KeyReader::Fretish,
     "Under nsga2-apportion, move a realizable candidate of the (mu + lambda) "
     "pool one front down when another realizable candidate is proved "
     "strictly stronger than it, slot by slot and by rule alone. A weaker "
     "timing or a removed guarantee costs nothing on similarity, so without "
     "this a gutted repair can sit on the first front beside the one it "
     "weakens. FRETISH only; the TLSF path has no rule to prove the order.",
     "FRETISH only, under nsga2-apportion: demote a realizable candidate one "
     "front\n"
     "when another realizable one is proved strictly stronger.\n"},
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
    {"mutation", "blame_targeted_slots", &Config::blame_targeted_slots,
     KeyCheck::None, KeyReader::Fretish,
     "FRETISH only: draw a rewrite's slot from where the parent's status "
     "points. An unrealizable parent draws from the guarantees its MRS walk "
     "refused plus every assumption, a realizable one from the slots that "
     "differ from the original; an empty list or a crossover offspring keeps "
     "every live weakenable slot. Costs no draw.",
     "FRETISH only: aim a rewrite at the guarantees the parent's MRS walk "
     "refused\n"
     "(and the assumptions) when the parent is unrealizable, or at the slots "
     "that\n"
     "differ from the original when it is realizable. Off by default.\n"},
    {"mutation", "direction_gated", &Config::direction_gated_mutation,
     KeyCheck::None, KeyReader::Fretish,
     "FRETISH only: aim a mutation's direction by the parent's status where "
     "crossover did not fire. An unrealizable parent keeps the weakening "
     "moves; a realizable one skips the added assumption and the removed "
     "guarantee and takes its directed moves from the steps back towards the "
     "original. A free rewrite moving the forbidden way returns the parent.",
     "FRETISH only: weaken an unrealizable parent and strengthen a realizable "
     "one\n"
     "towards the original, and return the parent when a free rewrite moves "
     "the\n"
     "forbidden way. p_sideways is the share of incomparable rewrites kept. "
     "Off by\n"
     "default.\n"},
    {"mutation", "p_sideways", &Config::p_sideways, KeyCheck::Probability,
     KeyReader::Fretish,
     "FRETISH only, under direction_gated: probability that a free rewrite "
     "incomparable with its parent is kept rather than returning the parent. "
     "Draws nothing at 0 or 1.",
     ""},
    {"mutation", "step_scaling", &Config::step_scaling, KeyCheck::Probability,
     KeyReader::Fretish,
     "FRETISH only: how far the parent's status scales the choice between a "
     "large move and a single edge. Each large move's probability (removing a "
     "guarantee, moving a timing off Always or Eventually, a tick-count jump) "
     "is multiplied by 1 + step_scaling * (1 - 2 * status). A crossover "
     "offspring is unscaled. Draws nothing at 0.",
     "FRETISH only: take more large moves from a parent far from realizable "
     "and\n"
     "fewer from one near it, scaled by the parent's status. Off (0) by "
     "default.\n"},
    {"mutation", "bisect_steps", &Config::bisect_steps, KeyCheck::None,
     KeyReader::Fretish,
     "FRETISH only, under direction_gated: a realizable parent's timing step "
     "on a within or for chain moves to the chain's midpoint rather than to "
     "the original's value. Costs no draw.",
     ""},
    {"model_counting", "default_bound", &Config::default_model_counting_bound,
     KeyCheck::Positive, KeyReader::Both,
     "Trace-length bound k for bounded model counting.", ""},
    {"filters", "run_implication", &Config::run_implication_filter,
     KeyCheck::None, KeyReader::Both,
     "Enable final implication maximality filter.", ""},
    {"filters", "climb_output", &Config::climb_output, KeyCheck::None,
     KeyReader::Fretish,
     "Strengthen each repair that passes the output gate towards the "
     "original before deduplication and the implication filter. A step is a "
     "one-field move towards the original that is proved a strengthening by "
     "rule, a restored guarantee or a dropped appended assumption, and it is "
     "kept only where the result passes the output gate again. The climb "
     "stops when no step is kept, and draws no random number.",
     "The lattice climb at the output: each gated repair is strengthened "
     "towards\n"
     "the original one proved step at a time, while it stays a repair.\n"},
    {"filters", "climb_order", &Config::climb_order, KeyCheck::None,
     KeyReader::Fretish,
     "Order in which the output climb tries its steps: 'slot' is slot order, "
     "assumptions first; 'similarity' tries the largest gain in syntactic "
     "similarity to the original first. The order decides which maximal "
     "repair the climb reaches. Read only when climb_output is true.",
     ""},
    {"filters", "climb_orders", &Config::climb_orders, KeyCheck::Positive,
     KeyReader::Fretish,
     "Climbs per repair. The first uses climb_order and each later one a "
     "fixed pseudo-random step priority of its own; every distinct result "
     "goes on to the final filters. Read only when climb_output is true.",
     ""},
    {"filters", "parent_verdict", &Config::parent_verdict_filter,
     KeyCheck::None, KeyReader::Fretish,
     "FRETISH only: after deduplication, drop each offspring proved stronger "
     "than a parent that an unrealizable query already asked proves "
     "unrealizable. Launches no query.",
     "FRETISH only: drop, before scoring, each offspring proved stronger than "
     "a parent\n"
     "an unrealizable query already asked proves unrealizable. Off by "
     "default.\n"},
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
    {"runtime", "ltlsynt_decompose_budget_ms",
     &Config::ltlsynt_decompose_budget, KeyCheck::NonNegative, KeyReader::Both,
     "How long, in milliseconds, a realizability query's first ltlsynt "
     "attempt, with SPOT's default decomposition, may run before it is killed "
     "and the query is asked again with --decompose=no in what is left of "
     "ltlsynt_timeout_ms. Default 1000; 0, or a value at or above "
     "ltlsynt_timeout_ms, asks once for the whole budget. Without "
     "decomposition lift's slow queries finish in a fraction of the time, "
     "while some of liquid-mixer's 5 ms queries time out, so neither setting "
     "wins alone.",
     "Milliseconds before a query is re-asked without decomposition.\n"
     "0 asks once.\n"},
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
    {"runtime", "controller_reuse_min_guarantees",
     &Config::controller_reuse_min_guarantees, KeyCheck::NonNegative,
     KeyReader::Both,
     "The fewest guarantees a FRETISH realizability query needs before the "
     "controllers ltlsynt emitted for earlier queries are tried on it ahead of "
     "a new exec; 0 turns the store off. Only a controller's pass is "
     "believed, so the store never makes a verdict wrong; it changes one only "
     "where ltlsynt's own was wrong, as SPOT 2.15.1's decomposition was on "
     "some queries.",
     "Guarantees a query needs before stored controllers are tried on it.\n"
     "0 turns the store off.\n"},
    {"runtime", "controller_reuse_min_screen_ms",
     &Config::controller_reuse_min_screen, KeyCheck::NonNegative,
     KeyReader::Both,
     "How long, in milliseconds, the input specification's own realizability "
     "query must take at the startup budget screen for the stored controllers "
     "to be tried; 0 tries them whatever it took. Default 40. A try costs "
     "about 20 ms of controller checks and saves an exec on 30-70% of tries, "
     "so it pays only where execs are slow. The screen took 8-10 ms on the "
     "small FRETISH specifications, where the store cost a quarter of the wall "
     "time, and 441-452 ms on lift, where it halved it. A run with no screen, "
     "as under an unlimited ltlsynt_timeout_ms, keeps the store.",
     "Screened input-query milliseconds below which stored controllers are "
     "not tried.\n"
     "0 always tries them.\n"},
    {"runtime", "mrs_lookahead_kill_grace_ms",
     &Config::mrs_lookahead_kill_grace, KeyCheck::NonNegative, KeyReader::Both,
     "How long, in milliseconds, a lookahead query of the MRS walk may keep "
     "running once the walk has settled its branch the other way, before its "
     "exec is killed; 0 kills at once. Read only when mrs_speculation_depth "
     "is above 0. A killed query records no verdict.",
     "How long an abandoned MRS lookahead query may run before it is "
     "killed.\n"},
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
