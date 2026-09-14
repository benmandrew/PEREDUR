# Performance

Memoisation keys, parallel dispatch, the implication prefilter and subprocess handling, with the measurements behind each.

## Cache keys

Every *memoisation* cache in front of an external tool is keyed on a rendered formula string, and `Formula` has no canonical shape, so each spelling of one formula bought its own subprocess. In `formula_key.hpp`, `canonical()` renders `Formula::canonical()` (commutative operands flattened, ordered and deduplicated, double negation dropped) and `renamed()` also renames the atoms to a canonical sequence. Both memoise on the raw input, so a spelling already seen costs a hash.

The key a cache may take depends on what it stores, and a wrong choice is silent. A verdict or a count is invariant under renaming atoms, so the satisfiability cache, `cached_count_traces` and `run_ganak_on_formula` take `renamed()`. A stored formula or automaton would need renaming back inside a tool's output, where SPOT prints `F` over an atom `fk1` as `Ffk1`, so `run_ltl2tgba_for_counting`, `simplify_ltl` and `rewrite_weak_operators` take `canonical()`. `RealizabilityChecker` takes `formula_key::realizability()`, a partition-preserving renaming plus the count of declared signals left unmentioned, those being part of the alphabet ltlsynt plays over.

The satisfiability cache keeps two keys in one map, tagged apart, because neither collapse contains the other: ltlfilt simplifies and the canonical form renames. Replacing the `normalised` key with the canonical one cost 2,773 execs against 2,211 on `rg2`. The raw-formula key is consulted first, which also skips the ltlfilt exec that `normalised` costs.

Keys must reparse, so the parser reads temporal operators. A letter operator counts only when the next character cannot continue an identifier, or `Grant` lexes as `G` over `rant`. External strings go through `Formula::try_parse`, since the constructor only asserts.

With the search held fixed over six specifications, the keys and the subsumption table remove roughly 13% of external-tool time, against an offline ceiling of 22% that `simplify_ltl` cannot reach without the renaming. Exec counts drift a percent or two per seed, two threads missing on one key both computing it; the repairs do not drift.

`count_guard_models` counts a Hanoi Omega-Automata (HOA) guard in process through `count_models_exhaustively` (`src/fitness/exhaustive_count.hpp`), a bitwise truth table, because the widest of 2,932 distinct guards over nine specifications mentions 12 atoms. It declines temporal or unparsable formulae and anything above `k_exhaustive_count_max_atoms` (16), leaving ganak as the fallback. Ganak execs fell to zero on all nine, and median wall time 50.1% pooled over seven paired families.

That figure is a paired median over three repetitions, the binaries interleaved per family. Run one arm after the other on the shared 20-core box, the same change read *slower* on seven of nine families, and one arm against itself moved 25.2%. Interleave the arms and read a median before concluding anything about wall time.

No simplifier pass may run over a guard before counting. `ltlfilt --simplify` drops a subsumed variable (`a | (a & b)` becomes `a`), while `count_guard_models` takes its free-variable exponent from the HOA label, so the count would silently fall by 2^(dropped). `test_ganak_counts_over_every_mentioned_variable` pins this, and `simplify_ltl` is the only entry point to that pass.

`RealizabilityChecker` also keeps a *subsumption table*. Realizability is monotone in both sides, so a query that drops a guarantee from a realizable specification, or adds one to an unrealizable one, is answered without an exec (and dually for assumptions). Conjuncts are interned and each side carries a 64-bit signature, so most entries fail one AND. Only decided entries subsume, and entries are scoped by atom partition and TLSF semantics. It removes 26.2% of ltlsynt execs over six specifications.

Sides are sound only where every conjunct has a determinate polarity in the lowered formula. TLSF wraps whole sections, yet in `theta_e -> (theta_s & [strict: psi_s W !psi_e] & ((G psi_e & phi_e) -> ([!strict: G psi_s] & phi_s)))` INITIALLY, REQUIRE and ASSUME are all negative and PRESET, ASSERT and GUARANTEE all positive, empty sections included. REQUIRE occurs twice, but the negation in `!psi_e` flips the second back, so both occurrences are negative; an earlier reading got this wrong, so check polarity through the negation before calling a section non-monotone. `tlsf::specification_sides` tags each conjunct with its section and puts the semantics in the scope. Well-separation queries build `(A) -> (false)` rather than `to_ltl()` and pass no sides, sides being valid only for a query over the lowering itself. `test_tlsf_subsumption_agrees_with_ltlsynt` (`test/tlsf/filter_tests.cpp`) checks both semantics against `ltlsynt`, since a wrong verdict here is silent.

`run.json` reports every memo's hits and misses under `caches`. Archived TLSF campaigns read `{hits: 0, misses: 0}` for the fitness cache, whose per-template counters then reported the FRETISH instance. `tool_calls.ltlfilt.calls` is `LtlfiltStats::n_execs()`, the set `total_s` covers. `cached_count_traces` absorbs repeats before the `ltl2tgba` and ganak caches, so their low hit rates (4.8% for `ltl2tgba` against 71.8% above it) are not a defect.

## Scoring dispatch

Every parallel region is a `run_bounded_async` call (`include/bounded_async.hpp`), and each is a full barrier: nothing after it starts until its slowest item finishes. A run at `generations = 10` spends 98-99% of its wall time inside 33 to 35 of them: per generation the vacuity filter, the score stage and, under `accumulate_repairs`, the per-generation gate (a serial sweep on FRETISH); per run the seed population's scoring, `conflict_degree_order`'s two passes under `mrs_admission_order = degree`, the final realizability gate and the implication filter. At 20 workers over four TLSF families, 5.4% to 13.0% of dispatch wall sat idle, all in the small regions; the O(n^2) implication filter takes 60-74% of wall at 99.1-99.4% *occupancy*. Campaigns run `parallel = 8`, so those idle figures are the pessimistic end.

**Longest-first launch.** `cost_ordered_indices` orders launches by descending cost estimate with index tie-breaks, and `run_bounded_async` takes it as an optional `launch_order`. Every call site collects results by index, so the order is unobservable. It bounds the *makespan* at (4/3 - 1/3m) of optimal for m workers.

**A candidate's score is no longer one task.** Scoring one candidate was a serial chain of subprocess calls on one worker, which no worker count shortens. `ObjectiveWork` and `FitnessPart` (`include/fitness/function.hpp`) split an objective into schedulable parts with cost hints, and `AggregateWeightedFitnessFunctionT` provides `cached_objectives`, `plan`, `store` and `scalar`. `score_population` (`include/genetic/generation.hpp`) plans the population without calling tools, then dispatches every part of every candidate in one region: one free syntactic part, one semantic part per changed slot, and a `black` query per component plus the realizability walk for status.

The walk's component short circuit is given up deliberately. The split path passes `ComponentCheck::Skipped` to `specification_status` and `tlsf_status` so that component queries are not asked twice, and a candidate with an unsatisfiable component therefore pays for its synthesis queries. On `lily11`, the family with the highest satisfiability miss rate, that costs 1.1% to 2.3% more `ltlsynt` misses; every other family's fell.

Repeats of one specification miss the fitness cache together, and scored populations are largely repeats (`stage_pad` and the apportion scheme both replicate). `plan_population` therefore folds repeats onto one plan, with `PlannedCandidate::indices` listing every slot it answers for. Without that fold the change is a net regression.

Over 12 paired runs across 4 TLSF families the repairs are byte-identical, 11 are faster at a median of about -3%, and non-pair occupancy rose 6 to 8 points. `arbiter-aurus` did not move, its straggler being the MRS walk, which cannot be split.

There is no config key, because scheduling changes no output and draws nothing from the `RandomSource`. It can shift which queries hit the `black` or `ltlsynt` timeouts, as any concurrency does; the 12 paired runs showed no such shift.

The implication filter already runs saturated, so the reachable target is the ~34% of wall spent in score stages. `make_predicate_filter`, the final gate and the implication filter accept a launch order, but none passes one yet.

## Implication prefilter

The implication filter already runs saturated, so it gets cheaper only by asking the solver fewer pairs. `fingerprint::prefilter` (`src/fingerprint/prefilter.hpp`) evaluates each lowered formula on 256 *lasso words* (ultimately periodic, at most five positions) and packs the results into a bitset. A word that A accepts and B rejects refutes `A -> B`, so `refutes_implication` compares two machine words. On a 421-candidate `round-robin-arbiter-aurus` set it refuted 95.9% of ordered pairs; the FRETISH rate is unmeasured.

It is sound because a word can refute an implication but never confirm one, so unrefuted pairs still reach the solver and output is byte-identical. For the same reason there is no config key.

Refuted pairs must be removed before dispatch. `run_bounded_async` bounds the items in flight, so a task that returns at once still takes a slot, and the sweep would pay O(n^2) scheduling anyway. `antichain::merge_subsumed` (`src/filter/antichain.hpp`) erases doubly-refuted pairs from `pairs` first. `implication.fingerprint_refuted` in `run.json` (`n_fingerprint_refuted`) is the only evidence the prefilter worked, because refuted pairs never reach `comparisons`.

Both paths share one template walk, since fingerprints compare only when drawn from the same words. The signals are `m_inputs` then `m_outputs` on TLSF, and `environment_signals` then `m_out_atoms` on FRETISH. Modes must be in the set: an atom that no word names is false everywhere, so each scope would be evaluated as if its mode never held. FRETISH lowerings reparse because `requirement_to_ltl` emits only X, G, F, U, W and R over propositional connectives. If `prop_formula_internal::try_parse_formula` cannot reparse a lowering, the table comes back empty and the sweep runs unfiltered.

### Behavioural fingerprints

`fingerprint` (`src/fingerprint.cpp`) prints the same evaluation as a hex row per input TLSF file: one bit per sampled lasso word, set where the specification's lowering holds at the word's first position. The Hamming distance between two rows estimates the measure of the two languages' symmetric difference, which separates a set of near-duplicate repairs from a set of genuinely different ones. The maximality filter cannot: an antichain is defined by an implication test no two of its members pass, so it says nothing about how far apart they are. Over one 211-repair `round-robin-arbiter-aurus` run, 211 pairwise-incomparable repairs are 90 that differ on a twentieth of sampled behaviours, 32 on a fifth and 3 on half.

Words are drawn from the *original* specification's signal list (`--signals`), the word count and the seed, never from a candidate's own, and the signals are sorted first, so two candidates of one family are scored on one word set however they reached the tool. Two fingerprints are comparable only when drawn from identical arguments. Nothing in the format records them, so a phase that compares two arms must state them once and use them twice. The binary is TLSF-only.

`score_curves.py --epsilon` turns this into an anytime curve: `eps_solutions_<e>` is the greedy net over the candidates accumulated by time t, in discovery order, keeping one only where it differs from every kept one on more than a fraction e of the words. Discovery order rather than fitness order makes the count non-decreasing, which an anytime curve has to be. It runs over every gate-passing candidate rather than over the maximal set, so it costs no solver call and removes an asymmetry the maximality curves carry: a timed-out implication reads as non-implication, and two tools' passes may run at different `compare` budgets.

Under `--maximality` the same net runs over the antichain as `eps_maximal_solutions_<e>`, counting repairs that are both maximal and distinct. It is computed where the maximality stage holds membership, in admission order, and the membership is written beside the curve (see "Scoring phases" in `docs/dev/campaigns.md`), so a later epsilon falls out of the sidecars with no solver call: over one `lily11` run all 45 `eps_maximal_solutions` rows reproduce from them alone.

## Tool subprocesses

Every pipe a runner opens must be created with `pipe2(..., O_CLOEXEC)`, as `execute_and_capture` (`src/runner/process.cpp`) and the formaliser (`src/runner/formaliser.cpp`) do. Runners are called from many threads, so a fork on one thread would inherit other calls' pipes past its exec, and their readers would never see end of file. `pipe` followed by `fcntl` races a concurrent fork. `dup2` clears the flag on the child's standard descriptors.

`spawn_piped_child` (`process.hpp`) is the one fork for any bidirectional child, so a second user differs only in `ParentDeathPolicy` and `ExecutableLookup`. Nothing may fork outside `process.cpp`, because `posix_spawn` has no attribute for `PR_SET_PDEATHSIG`.

A tool's peak resident set size (RSS) cannot be measured below PEREDUR's own: `exec` folds the copy-on-write parent's high-water into the child's `maxrss`. `ProcessResult` therefore samples `m_peak_rss_floor_kb` before the fork and reports `m_peak_rss_kb` as zero at or below it. `tool/<name>/rss_*` counts every invocation in `calls` but only floor-clearing ones in `rss_measured`, so a mean is `rss_kb_total / rss_measured`. The `process_runner` suite pins this with a 512MB buffer.

`simplify_ltl` runs one `ltlfilt` exec per cache miss. Coalescing concurrent misses was tried and removed: at `parallel = 8` the mean batch size was 1.012, and wall time rose 37% on 46 of 46 paired examples. Any second attempt must first show a batch size above 1.

## Profiling

`PEREDUR_PROFILE=<path>` enables the *scope profiler* (`include/profile.hpp`): a table on stderr plus JSON at the path, while `PEREDUR_PROFILE=1` gives only the table. The report registers with `atexit` on the first scope opened, so every binary reports without extra wiring. The `peredur` drivers also call the idempotent `profile::report_if_enabled()` so the profile prints before the manifest and `Done in`.

Read wall time against per-thread CPU time. A site with high wall time and near-zero CPU is waiting on a child process, as `proc/read` shows at a cpu/wall ratio of about 0.01.

The profiler is in-process because `kernel.perf_event_paranoid=4` and yama `ptrace_scope` rule out `perf` and `gdb` on the dev box, and `strace` can only launch a process, never attach. The counter registry is deliberately leaked so that it outlives the `atexit` report that prints its names.
