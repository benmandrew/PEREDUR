# A failure budget for filters

Status: built on 2026-11-16, as described under "What was built" at the end. Written after #117 and #119 fixed the two call sites where a raising external tool was cheapest to handle, leaving the harder half unaddressed.

## The problem

An external tool call can throw from four kinds of place. Only one of them handles it.

Fitness functions run inside the scoring pool, and `score_population` collects their errors and tolerates up to `Config::max_scoring_failure_rate` of the population — 0.15 — before aborting with "the fitness tooling is broken rather than the formulae". Seven call sites are covered this way.

Filters have no equivalent. Neither does the final survivor gate. The input screen had none until #119. A throw from any of them reaches the driver's outermost handler, which prints `fatal:` and returns 1, discarding every generation already paid for.

The `2026-08-11-selection-default` campaign priced this. SPOT 2.15.1's `ltlsynt` aborts with `Too many acceptance sets used. The limit is 32.` on specifications the search reaches unaided, and 130 of the campaign's 1,500 TLSF runs hit it. 115 absorbed it in the scoring pool and finished. 15 hit it in a filter and died. The error was the same; only its position differed.

## Why a blanket catch is the wrong fix

Two reasons, and the second is the one that matters.

A catch has to resolve the call to some value, and the safe direction differs per site. `include/runner/spot.hpp` states this where `check_realizability_ltl` is declared: nullopt means undecided, "unrealizable" is the safe reading where a true answer admits a repair and the unsafe one for the well-separation check, and the choice is spelled with `value_or` at each call site. No generic handler can infer which way to go, and going the wrong way at a correctness filter admits specifications nothing verified.

The larger risk is losing the signal. A run whose toolchain is broken — `ltlsynt` missing, `black` unbuilt — fails today in seconds with a clear message. Under blanket catches that run completes, drops every candidate, and reports zero repairs, which is indistinguishable from a genuinely hard specification. The tolerance in the scoring pool exists precisely to keep those two apart, and any filter-side handling has to preserve it.

## The proposal

Give filters the same budget the scoring pool has, at the pipeline stage boundary rather than inside any one filter.

`run_generation_pipeline` (`include/genetic/pipeline.hpp`) already runs an ordered vector of named `PipelineStage`s and reports each to an optional `StageObserver`. That is the level where a budget belongs, for a reason that matters: `make_predicate_filter` looks like the natural chokepoint, but it covers only the FRETISH predicate filters. `src/filter/implication_check.cpp` is a population-level sweep, and the three `src/tlsf/filter.cpp` sites build `FilterFunctionT` directly from a whole-population lambda. A budget in the predicate wrapper would miss more sites than it caught.

Each stage would then carry, alongside its name and `FilterKind`, a policy for what an undecided element means — the thing currently spelled ad hoc in each filter's `.value_or(...)`. `FilterKind` already separates `Correctness` from `Preference` and is the natural place to hang it.

The budget itself mirrors `max_scoring_failure_rate`: count failures within a stage, resolve each element by that stage's declared policy, and abort the run above a threshold with a message naming the stage and the tool. A single shared key is probably right rather than one per stage, since the thing being detected — a broken toolchain — is not per stage.

## What this does not cover, deliberately

The final survivor gate keeps throwing. It is the enforcement point, so swallowing an error there means writing a repair nothing checked, and that is worse than losing the run. `is_realizable_repair` short-circuits on the memoised status score, so `first_failing_check` only runs for candidates already about to be written — the loss is real but it lands on a completed search, and no budget makes emitting an unverified repair acceptable.

The standalone binaries (`mucs`, `realize`, `lint_ideals`) are one-shot tools where terminating on a tool failure is the correct behaviour and the message is already the output.

## Handling is not prevention

The acceptance-set limit is a SPOT build constant, and `--enable-max-accsets=64` in `cmake/spot.cmake` would remove most of those 130 occurrences rather than absorbing them. It is not free: `mark_t` has a template specialisation making it a single `unsigned` at exactly 32, and above that it becomes a `bitset` looped over words, a cost every automaton operation in the process pays. It also breaks the engine freeze, so it cannot land on a campaign host without re-attributing provenance.

The two are complementary. The budget stops any tool failure ending a run; the rebuild stops this particular failure happening. Neither substitutes for the other.

## Open questions

Whether one budget key or one per stage. Whether a stage that exhausts its budget should abort the run or drop out of the chain for the rest of the generation, given the filter fallback already re-applies `FilterKind::Correctness` filters when the chain empties the offspring. And whether a raising query should be memoised as undecided, as timeouts already are — today the throw precedes the cache insert, so every re-encounter re-execs the tool.

## What was built

The budget sits in `make_predicate_filter` rather than at the stage boundary. Since this note was written, both front ends build their per-generation correctness filters from the one `correctness_checks` table through that wrapper, so it now covers every per-generation site. A candidate whose check raises is rejected, the safe reading for a correctness check, and is tallied in `FilterErrorStats`. Above `tolerated_failures(max_scoring_failure_rate, n)` of one call's input, the filter throws a message naming itself and the first error. The key is the scoring pool's, for the reason given above: what it detects is a broken toolchain.

Two sites outside the per-generation chain read an error the way they already read a timeout. `implication_query`, shared by `spec_implies` and `tlsf_spec_implies`, reads one as undecided, and the output climb's gate refuses the step, which leaves the climb on a repair that has passed the gate. The final survivor gate and the start-up checks still throw, for the reasons given under "What this does not cover".
