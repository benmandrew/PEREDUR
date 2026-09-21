Configuration
=============

Algorithm parameters — population size, fitness weights, mutation rates and so on — are tuned without recompiling by passing a *Tom's Obvious Minimal Language* (TOML) file to ``--config``:

.. code-block:: sh

   peredur --input spec.json --output-dir out --config my-config.toml

Every section and key is optional; absent keys keep their built-in defaults. Sections and keys the parser does not recognise are ignored, each reported on stderr by its full path, so a misspelling shows up as ``config: unknown key genetic.mutaton_rate, ignoring`` rather than as a setting that silently did nothing. A key placed in the wrong section is reported the same way. Values still out of range for their field abort the run.

.. code-block:: toml

   [genetic]
   generations     = 20   # double the default evolution rounds
   population_size = 500

   [runtime]
   parallel = 16              # override thread pool size
   dashboard = true           # stream progress.jsonl + the live dashboard page

``example-config.toml`` in the repository root is an annotated template listing every key with a comment explaining it. Every value in it **is** that key's default, so copying the file whole changes nothing; ``scripts/check_config_schema.py`` enforces this against ``include/config.hpp`` as part of the ``lint`` target.

Fitness weights
---------------

Three components are combined into each candidate's score, and realisability status dominates. The defaults follow AuRUS's ``Settings.java``, which weights status 0.7, its lost-model and won-model terms 0.1 each, and syntactic similarity 0.1; PEREDUR's semantic similarity stands in for the two model terms together at 0.2.

.. list-table::
   :header-rows: 1
   :widths: 30 15 55

   * - Key
     - Default
     - Component
   * - ``fitness.weight_semantic``
     - 0.2
     - Bounded model counting of satisfying LTL traces
   * - ``fitness.weight_status``
     - 0.7
     - ``black`` + ``ltlsynt`` outcome, mapped to {0, 0.5, 1.0}
   * - ``fitness.weight_syntactic``
     - 0.1
     - Shared sub-formula count, normalised to [0, 1]
   * - ``fitness.mrs_admission_order``
     - ``"degree"``
     - Order the ``"mrs"`` greedy walk admits parts in: ``"spec"`` or ``"degree"``
   * - ``fitness.status_grading``
     - ``"mrs"``
     - Scale the status component grades on: ``"tiered"``, ``"mrs"`` or ``"aurus"``

``fitness.status_grading`` decides how finely the status component grades the region below realizability. ``"tiered"`` is the three-point scale above. ``"mrs"`` replaces its middle tier with the greedy maximal-realizable-subset fraction: the guarantee side is split into parts, and the score is the fraction of them that can be kept while the accumulated subset stays realizable against the full, unchanged environment side. Both keep 1.0 meaning realizable and well-separated.

Three levels leave a genetic algorithm little to climb. Over the 21 specifications under ``examples/`` the tiered scale scores every one of them 0.5, where the MRS scale spreads them over 14 distinct values, with a median of 6 grade levels per specification and a maximum of 17. It costs more ``ltlsynt`` queries per candidate — a median of 4.6x one whole-specification check measured alone, falling to about 2.2x over a population, since the greedy walk's prefixes recur across near-identical candidates and hit the memoised checker. The campaign that crossed the two ran on 2026-08-11, paired over 20 TLSF specifications at 24 seeds each. ``"mrs"`` found a repair in 410 of 480 runs against ``"tiered"``'s 367, on 50 discordant pairs against 7 (sign test p < 0.0001), and repair quality did not move on the runs where both arms yielded. The median paired wall cost is 1.15x, and ``"mrs"`` is the default from that campaign on.

``"aurus"`` is the third value, and the odd one out: it reproduces AuRUS's own status ladder value for value rather than improving on it, so that an ablation can cross PEREDUR's grading against the design PEREDUR derives from. It scores 0 where neither side of the candidate is satisfiable, 0.05 where only the guarantee side is, 0.1 where only the assumption side is, 0.2 where the two are each satisfiable but contradict each other, 0.5 where they hold together but no strategy exists, and 1.0 where one does. A satisfiability query that comes back undecided scores 0, as AuRUS pays its UNKNOWN status what it pays its BOTTOM one; an undecided realizability query scores 0.5.

The divergence to know about is that ``"aurus"`` does **not** fold well-separation into its realizability query, where both other scales do. AuRUS never asks the question anywhere in its search, so a candidate realizable only by forcing its own assumptions to fail scores 1.0 under this scale and 0.5 under ``"tiered"``. That is the whole point of the arm: an ``"aurus"`` scale that graded well-separation would not be the thing it is named after. It changes what the search is paid for and nothing else — the final gate asks every correctness check unconditionally whatever the search scored on, so output correctness is identical under all three scales.

``fitness.mrs_admission_order`` is read only under ``status_grading = "mrs"``, and chooses the order the greedy walk admits guarantee-side parts in. ``"spec"`` is the specification's own index order, which the walk shipped with. ``"degree"`` sorts the parts by ascending pairwise-conflict *degree* — how many other parts a part cannot be held together with — so a part that blocks many others is admitted last. Ties keep index order, and a part unrealizable on its own sorts last. It is the min-degree heuristic from constraint satisfaction.

Greedy returns a maximal subset rather than a maximum one, so the order decides the score. Index order is measurably biased by one structure, a single early part conflicting with the rest of the guarantee side. On ``detector`` index order keeps 1 part of 7, where deferring that part keeps 6. Cost turns on the order being the *same* for every candidate in a run, which is what seed reproducibility and the memoised realizability checker both need. Measured over populations of mutants across six TLSF specifications, every fixed order costs 1.02x to 1.07x index order's ``ltlsynt`` execs, against 1.48x for a fresh order per candidate.

``"degree"`` scores 0.587 against index order's 0.529 at 1.02x the execs, and scored no lower than index order on any of the six specifications. Choosing it costs ``n(n-1)/2 + n`` subset queries once, before the search starts. That is 28 on a 7-part specification and 136 on a 16-part one, against a run that scores tens of thousands of candidates. ``"degree"`` is the default from that measurement on. That is weaker evidence than the paired campaign that settled ``status_grading``, since everything measured so far scores mutants in isolation with no selection pressure, so nothing yet says what the finer gradient does to yield or ``implies_ideal``. A campaign reading both is still owed. The measurements are TLSF-path only. The FRETISH path accepts the same key, where its parts are whole guarantees.

Semantic similarity is the expensive one: it counts the satisfying traces of a candidate up to ``model_counting.default_bound`` (default 20) using Ganak over the transition matrices of SPOT-generated automata. Raising the bound sharpens the measure and costs time.

The status scale is three points and coarse on purpose. 0 means some component formula of the candidate is unsatisfiable on its own; 1.0 means the candidate is realizable and *well-separated* with it; 0.5 is everything in between — either no strategy exists, or the only strategy is to force the candidate's own assumptions to fail. Those last two share a tier rather than being graded apart, because a tier between them would rank cheating above failing. Ill-separation is cheap for mutation to reach and expensive to leave, so an intermediate score would be a broad plateau the search settles on — the shape the aggregate already shows for gutted vacuous repairs. Scoring it level with unrealizable keeps such a candidate in the population as breeding material, which is the value of not dropping it, and pays it nothing for the cheat. Grading *within* unrealizability instead needs a measure of how far a candidate sits from realizable, which a satisfiability query cannot express.

Both front ends fold the well-separation query in behind the realizability one, so it is asked only where it can change the answer: a candidate already unrealizable cannot be realizable for the wrong reason. No per-generation filter drops an ill-separated candidate before it is scored, so the 0.5 tier is the whole of the search's pressure against ill-separation; see `Filters`_.

Similarity metric
-----------------

``model_counting.metric`` chooses how those trace counts become a score. Both options take the harmonic mean of two directional containment terms — how much of each requirement's satisfying traces the other also accepts — and differ only in that term.

**direct** uses the ratio of counts: the fraction of one requirement's satisfying traces that also satisfy the other, a Sørensen–Dice overlap. It is a true overlap measure, but because trace counts grow like ``λ^k``, the ratio decays toward zero as the bound grows for requirements of differing permissiveness — so the score depends on ``default_bound``.

**logarithmic** (default) uses the ratio of the counts' *logarithms*, comparing the languages' growth rates rather than their overlap. That ratio tends to a constant as the bound grows, so the score is stable across bounds, at the cost of the direct overlap interpretation. Useful when comparing runs at different bounds, or when the bound is driven by timing horizons that vary between candidates.

Selection scheme
----------------

``genetic.selection_scheme`` decides how those four components drive selection. The two NSGA-II schemes rank identically and are named for the survivor step, the only thing that differs between them.

.. note::

   These were spelled ``nsga2`` and ``nsga2-replicate`` before 2026-08-06. Both spellings are now **rejected** rather than accepted as aliases, so a config setting one fails with an error naming its replacement. To reproduce an archived campaign, build the commit its ``PROVENANCE.json`` records rather than rewriting its config.

**nsga2-truncate** treats them as separate objectives and ranks candidates by *Non-dominated Sorting Genetic Algorithm II* (`NSGA-II <https://doi.org/10.1109/4235.996017>`_): Pareto non-domination first, then crowding distance to spread the population along the front. It searches for the whole Pareto front — the repairs not beaten on every objective at once — rather than one weighted compromise. That is useful when the right balance between, say, semantic similarity and size is not known in advance.

**nsga2-apportion** (default) ranks exactly as ``nsga2-truncate`` does, but changes what the (μ+λ) survivor step keeps. The pooled parents and offspring are mostly repeats of a handful of specifications, and duplicates never dominate one another, so they inflate the rank-0 front until truncating the pool back to ``population_size`` cuts through that front arbitrarily. Deduplicating the pool on its own is no fix: it leaves a handful of survivors, selection becomes a no-op, and breeding collapses. Instead the pool is deduplicated, ranked, and — where the distinct set is smaller than ``population_size`` — replicated back up to it, weighting each individual by ``1 / (1 + rank)`` with a floor of one copy and handing out the spare slots by *largest-remainder* (Hamilton) apportionment. Selection pressure is re-expressed as how many slots an individual holds rather than whether it survives at all. The step is purely arithmetic and draws no random numbers, so a seeded run stays reproducible.

The measured effect is diversity. An A/B over the four FRETISH examples at ``generations = 10``, ``population_size = 200`` and ``elitism_rate = 0``, with 20 seeds each for 80 paired runs, raised the distinct candidates held at generation 9 from 5.9 to 98.3, ``n_repairs`` from 3.43 to 9.94, and the share of runs finding any repair at all from 0.966 to 1.000 — for roughly 50% more wall-clock time. Repair quality did not move: pooled ``implies_ideal`` was 0.500 under both schemes. That figure is uninformative here rather than reassuring, because the corpus sits at its extremes — ``takeoff`` and ``fsm-timing`` score 1.000 either way, ``fsm`` and ``fsm-combined`` score 0.000 either way — so it leaves no headroom in which a quality difference could show. The diversity gain is measured; the quality gain is unproven.

The ``2026-08-11-selection-default`` campaign settled the current default over 2,325 paired runs. Apportion finds a repair more often on both paths: 0.654 to 0.704 on FRETISH, 0.857 to 0.887 on TLSF. It returns more of them, with ``n_repairs`` up on 19 of 20 TLSF specifications by 1.82 on average. TLSF quality rises by 0.055 pooled, better on 6 specifications and worse on none. The cost is 1.35x the FRETISH wall time and 1.15x the TLSF.

The loss is FRETISH quality. ``fsm-timing`` falls from 70 runs matching an ideal to 64, and ``takeoff`` from 69 to 66, against 4 gained on ``fsm``. That is 5 runs in 280. The same 280 gain 14 runs that find a repair at all: 19 on ``fsm-combined``, which ``nsga2-truncate`` repairs in no run, against 5 lost on ``fsm``.

This default was changed against the campaign's pre-registered rule, which returned *no*. Three of its five criteria failed. FRETISH quality missed against a compute-matched ``nsga2-truncate`` arm, at −0.0250 with the interval reaching −0.0536. The yield gain missed against both comparators, whose intervals include 1; on TLSF against the compute-matched arm the odds ratio is 1.786 at p = 0.078. Those failed on interval width rather than direction. Every yield point estimate favours apportion, and per unit of extra compute it beats spending the budget on generations: 5.0 percentage points at 1.35x on FRETISH, against the extra-generations arm's 2.5 at 1.32x.

Two limits remain. ``implies_ideal`` does not vary at all on 13 of the 20 TLSF specifications, 12 of them pinned at 0, so the quality criteria are blind across most of the corpus — ``fsm-combined`` included, where apportion's largest yield gain lands. The ``elitism_rate`` interaction went untested here, pinned at 0.1 throughout, and apportion deduplicates the pool that elitism refills with copies. The 2026-08-23 ``monotone`` campaign has since compared 0.1 against 0 at ``nsga2-apportion`` over 500 paired TLSF runs and found the two level on yield and on quality; see `Search size and operators`_.

The comparison against ``weighted`` on TLSF is a different picture. The ``2026-07-24-ablation`` campaign crossed those two schemes over 21 TLSF specifications at 80 seeds each, 3,076 runs, and they split the two measures between them:

.. list-table::
   :header-rows: 1
   :widths: 34 22 22 22

   * - Measure
     - ``nsga2``
     - ``weighted``
     - Winner
   * - Found any repair
     - 0.764
     - 0.868
     - ``weighted``
   * - Matched an ideal (all runs)
     - 0.245
     - 0.148
     - ``nsga2``
   * - Matched an ideal (runs that found one)
     - 0.321
     - 0.170
     - ``nsga2``

Per specification the yield difference is narrow but lopsided where it lands: 15 ties, 4 wins for ``weighted``, 1 for ``nsga2``. Two of those wins are total. On ``arbiter`` — the GR(1) two-client arbiter, whose repair needs a fairness assumption added — ``nsga2`` finds nothing in 80 runs and ``weighted`` finds a repair in all 80, matching an ideal in 31 of them. On ``rg1`` the counts are 4 of 80 against 80 of 80. Both still hold at the current default: on ``arbiter`` at seed 42, ``nsga2-truncate`` returns no repair and pins at fitness 0.807692, while ``weighted`` returns 22 realisable survivors and 6 maximal repairs at 0.928367.

So the schemes are not ordered on TLSF, they trade. ``weighted`` finds a repair more often; ``nsga2`` finds the *right* repair about twice as often, on either denominator, and on ``lily02`` the gap is stark — both schemes repair it in nearly every run, but ``nsga2`` matches an ideal 80 times out of 80 against ``weighted``'s 39. Treat a TLSF specification that yields nothing under the default as a candidate for ``weighted`` rather than as unrepairable, and read what comes back with the quality figures above in mind.

**weighted** collapses them into a single weighted average and ranks by that scalar, using truncation selection with elitism. In principle this finds the one repair that best fits the configured trade-off; in practice it converges prematurely and then stagnates. Over a 50k-run parameter sweep its results did not move with the generation count at any level from 5 to 80, and on the ``takeoff`` example it matched an ideal repair in 1.7% of runs against ``nsga2-truncate``'s 89.3%, at no saving in wall-clock time. On the FRETISH corpus it is kept for comparison rather than for use; on TLSF it is the scheme of last resort for a specification the default cannot repair at all, at the cost of repair quality noted above.

Two consequences are worth knowing. Under either NSGA-II scheme the ``[fitness]`` weights only decide which components are active (weight > 0); they no longer bias selection. And their survivor selection pools each generation's parents with their offspring and keeps the best — a (μ+λ) scheme, already elitist — so ``elitism_rate = 0`` is the natural companion setting on paper. It measured slower in both campaigns that have tested it, which is why the default is 0.1; see `Search size and operators`_.

All three schemes still emit the weighted-average scalar in each repair's fitness record, so outputs stay comparable across runs.

Search size and operators
-------------------------

``[genetic]`` sizes the search. ``generations`` and ``population_size`` are the two that matter. ``selection_rate`` (0.5) is the share of the population that breeds, and ``elitism_rate`` (0.1) the share carried into the next generation verbatim, bypassing crossover, mutation and the offspring filters. Elites are a subset of the parents, so ``elitism_rate`` must stay strictly below ``selection_rate``. ``crossover_rate`` (0.1) and ``mutation_rate`` (1.0) are the per-offspring probabilities of applying each operator.

``elitism_rate`` looks redundant under the default scheme and is kept anyway. Under either NSGA-II scheme the (μ+λ) survivor step already pools parents with offspring and keeps the best, so an elite carry-over on top adds nothing in principle, and it is not free either, because elites bypass the offspring filter chain and that fraction of every generation therefore skips the vacuity stage. What the bypass costs is search pressure rather than output correctness — the gate over the collected realizable survivors screens whatever an elite carried in, whichever way this key is set (``d7733fc``). Both arguments say 0.

The first campaign to test it ran on 2026-08-07 at ``nsga2-truncate``, over 600 paired FRETISH runs (4 examples, 150 seeds each) and 796 paired TLSF runs (20 families, 40 seeds each), against a rule fixed before launch. Quality was non-inferior at 0: paired ``implies_ideal`` moved by −0.023 on FRETISH and −0.008 on TLSF, both intervals excluding the +0.05 margin. Yield moved the other way on TLSF, where 0 found a repair in 0.746 of runs against 0.714 at 0.1, on 37 discordant pairs against 11 (McNemar p = 0.0002). Running at 0 cost 16.2% more wall time on TLSF and 8.2% more on FRETISH, against a bound of 10%, and that cost is what held the default at 0.1.

That TLSF yield advantage did not replicate. The 2026-08-23 ``monotone`` campaign (``experiments/2026-08-23-monotone``, TLSF sweep T) crossed the same two values at ``nsga2-apportion`` over the 25-family AuRUS TLSF corpus, 500 paired ``(spec, seed)`` runs, with ``accumulate_repairs`` on in both arms and under the monotone operator grammar. Its two arms differ in ``elitism_rate`` alone. Yield is 472 of 500 at 0.1 against 470 at 0, on 26 timeouts against 29.

Quality is level on every read of it. Per-family ``implies_ideal`` moves 0.510 → 0.508, at exact Wilcoxon p = 0.7188 over 9 non-tied families. The clustered read moves 0.465 → 0.470 at p = 0.6250, and that number is uninformative by construction: it has 5 non-tied clusters, so its exact two-sided floor is 2/2⁵ = 0.0625 and no p below 0.05 exists there at all. Run level, exact McNemar reads p = 1.0000, with 0 gaining 28 runs of 500 and losing 29.

The wall-time cost did replicate, four times smaller. Running at 0 costs 4.0% more — 101.1 h against 97.1 h over the same 500 runs, and a median of 49.0 s against 42.2 s, at exact Wilcoxon p = 2.9e-15 over 491 non-tied pairs. The gap narrowed because ``accumulate_repairs`` is on in both arms, so a repair found in an early generation is kept whether or not an elite carried it forward.

So the only measured difference between the two settings, on the corpus PEREDUR is now benchmarked against, is that 0 costs 4.0% more wall time, and the default stays at 0.1. Only 0 and 0.1 have ever been measured, so nothing here speaks to intermediate rates.

The gutting worry that once justified 0.1 is retired, and how it died is worth recording. A post-hoc audit lowered the guarantee side of all 9,814 written repairs and tested each conjunct with ``ltlfilt --equivalent-to=1``: 3 runs in the 0 arm and 4 in the 0.1 arm had written a guarantee that reduces to ``true``. Appearing in *both* arms, it measures the screen rather than elitism. Every one of those guarantees contains a weak until, and ``black`` answers ``SAT`` on the negation of such a formula where ``ltlfilt`` proves it valid, so the vacuity screen was told the specification constrained something and admitted it. Rewriting ``W`` away before the query fixes it (``fb4c3ed``), and the audit script is vendored at ``experiments/2026-08-07-elitism/scripts/triviality_audit.py``.

``accumulate_repairs`` (on) changes what the run reports rather than how it searches. PEREDUR otherwise emits the maximal antichain of its *final* population, so a candidate that passed the output gate in generation 3 and was not selected into generation 4 is a repair the search found and then threw away. With the key on, every gate-passing candidate of every generation is kept — deduplicated, uncapped — and unioned with the final population's own collection before the final filters run. Since a repair set is judged existentially, by whether it contains a repair implying the ideal, a larger pool can only help. It is the behaviour the AuRUS baseline has, and the reason AuRUS emits a median of 448 solutions where PEREDUR emits 4.

The default moved on 2026-08-25, on the ``2026-08-19-accumulator`` campaign: TLSF only, 200 paired runs at 100 ``(spec, seed)`` pairs per arm, against a rule fixed before launch. Both arms yielded ``implies_ideal`` on 29 pairs and neither on 65, the accumulating arm alone on 6 and the control alone on 0, at exact McNemar p = 0.0312 over the discordant pairs. Yield moved from 79 to 81 of 100. The median paired wall ratio was 1.034 (mean 1.055, max 1.51) against a cost bound of 1.25, and timeouts were identical at 16 per arm, so the cap censored both arms alike and the pairing holds.

That campaign made ``accumulate_repairs`` a *candidate* default, subject to the FRETISH replication its plan names, and that replication has not been run. The default is flipped on TLSF evidence alone, as a deliberate departure from the campaign's own pre-registered condition, and the FRETISH replication is still owed. Two limits narrow what the TLSF evidence covers. Its 240 s per-spec cap falls between the aurus-h2h corpus's p75 of 161 s and its p90 of 684 s, so it cannot say whether the accumulator's advantage grows with the budget; and four seeds per family is thin, the primary drawing its power from the 200 paired runs rather than from within-family precision, so no per-family claim should be read off it.

Every archived config bar that campaign's own two arms omits the key, so each of them now means something it did not, which the *config vintage* note in ``experiments/README.md`` records. The accumulator never draws from the ``RandomSource``, so the seed stream is byte-identical whichever way the key is set and only the emitted set differs. The accumulated members are merged rather than re-checked: they passed the same gate, in the generation they were collected in.

The key buys the extra repairs with one gate sweep per generation, on both paths. The realisability query behind that sweep is memoised from scoring, so the added cost is mostly the correctness rows. With the key off neither path sweeps at all, which the FRETISH path is visibly poorer for: its status line's ``real`` column is read off the sweep, so the column is printed only where accumulation is on. It ran the sweep unconditionally until 2026-09-09, on the argument that the column needed it anyway, which made the column cost a gate query per candidate per generation and left ``accumulate_repairs = false`` saving nothing there. Under ``repair_mode = "muc"`` the key is inert, and deliberately: that mode evolves a *core* sub-specification, so a gate-passing candidate of it is realisable against the core alone and emitting one would report a fragment as a repair of the whole specification.

With the key on, each newly accumulated specification is written to ``<output-dir>/accumulated/`` the moment it is accumulated, one file per specification, named ``gen<NN>_<seq>.json`` on the FRETISH path and ``gen<NN>_<seq>.tlsf`` on the TLSF one. Every file is opened, written and closed on the spot, the same intent as the ``progress.jsonl`` the dashboard reads, so a run killed by an external wall-clock cap keeps every repair it had already found. That is the failure the writer exists for: in the aurus-h2h campaign 126 AuRUS runs were killed at the 7200 s cap and lost 3,934 solutions their logs prove had been found, and 19 of 499 PEREDUR runs were killed the same way. The directory is created on the first write and not before, so a run with the key off leaves the output directory exactly as it was.

What lands there is the raw set of gate-passing candidates. ``repair_N.json`` and ``repair_N.tlsf`` are unchanged and remain the only filtered output, deduplicated and screened for maximality, with a fitness record beside each. An accumulated file is a specification document alone, written through the serialiser a repair goes through, so a tombstoned guarantee is absent from it rather than flagged.

``run.json`` records ``n_accumulated_repairs``: how many repairs the accumulator contributed that the final population's own collection did not already hold. It reads 0 with the key off, and also when every accumulated repair happened to survive to the last generation, so read it against the config key rather than on its own.

The final implication filter runs **during** the search rather than after it, wherever ``accumulate_repairs`` and ``run_implication`` are both on, and on the TLSF path also ``repair_mode = "monolithic"``, MUC mode accumulating nothing. Both inputs have to be there for there to be a stream at all: nothing queues without the accumulator, and there is nothing to run without the filter. Each repair the accumulator collects is pushed onto a queue as it is accumulated, and a coordinator thread of its own takes whatever has queued at each wake-up and merges it into the maximal set found so far. The coordinator's solver checks run on the thread pool's background queue, where the search's own scoring and filters run on the foreground one; a worker takes a background task only when the foreground queue is empty, so the search waits on at most the background checks a worker already holds and never on one still queued. What remains to merge once the search ends is the last batch plus any realisable specification the final population's own collection found that no generation had already accumulated, and ``repair_N.json`` / ``repair_N.tlsf`` are unchanged — the same maximal set, numbered in the same order a batch filter would give.

Nothing accumulated is deleted; every file still lands in ``<output-dir>/accumulated/``, and beside them ``accumulated/maximal.tsv`` lists the files currently maximal — a ``file`` header line, then one name per line in accumulation order — rewritten after every batch through a temporary file and rename, so a run killed mid-write keeps the previous batch's listing, and a write failure warns once and the run continues without it. Streaming reaches the same antichain as one sweep over the union wherever no implication check times out, the settled members already forming an antichain so only pairs touching a new arrival are asked; a timed-out check reads as non-implication on both paths, but the two ask different pairs, so a run with timeouts can differ. ``run.json``'s ``implication.comparisons`` and ``implication.skipped`` then count the streaming sweep and do not compare against a batch run's figures, and ``implication.duplicates`` reads 0, the stream deduplicating before a specification queues. There is no config key selecting between the two routes, on the same argument the implication prefilter and the scoring dispatch are recorded under: a change that moves cost and no output gets one code path, so restoring the batch route means reverting the commit that made streaming the route. Manifest ``k_schema_version`` went 27 to 28 for the figures it reports, and it draws nothing from the ``RandomSource``, so the search's breeding stream is unchanged. Over 6 paired runs (``fsm``, ``fsm-timing`` and ``takeoff`` at seeds 0 and 1, gen10/pop200, arms interleaved per case) wall time falls 11.7% pooled, 15.55s to 13.74s, faster on 6 of 6 cases and between 4.2% and 17.8% per cell, with ``n_repairs`` and ``n_accumulated_repairs`` agreeing on every one and no check timing out on either arm. No campaign has measured it at campaign search sizes.

``[mutation]`` weights what a mutation does. ``p_trigger`` (0.5), ``p_response`` (0.5) and ``p_timing`` (0.15) select which part of a FRETISH requirement is rewritten, and ``p_condition_type`` (0.15) and ``p_scope`` (0.15) reach the two fields those three leave alone. ``p_monotone`` (0.25) sits inside the first two rather than beside them, deciding how the field one of those two lands on is rewritten. ``p_stop`` (0.15) rewrites the stop condition of an ``until`` or ``before`` timing, moving it the way the requirement is meant to move: the stop occurs positively in ``until`` and negatively in ``before``, and an ``only`` scope swaps the two. It is read only on a requirement holding a stop timing, before any random draw, so on a specification without one it changes nothing at any value.

``p_add_assumption`` (0.05) is the structural operator shared by both paths: rather than rewriting an existing requirement, it appends a new environment assumption. It is the only way the search can repair an unrealisability that needs the environment strengthened — adding a fairness assumption to an unrealisable *generalised reactivity of rank 1* (GR(1)) specification, say. The rewrite-only operators cannot express that move. Of the assumptions it appends, ``p_conditional_assumption`` (0.25) is the fraction guarded by a random atom rather than by ``true``; ``G F <input>`` is strictly stronger than ``G(c -> F <input>)`` and so the more effective repair, which is why the unconditional form keeps most of the draw.

``p_remove_guarantee`` (0.05) is the mirror of ``p_add_assumption``, shared by both paths. Rather than rewriting a requirement, it deletes one guarantee outright — a FRETISH guarantee requirement, or a TLSF guarantee-side conjunct from ``PRESET``, ``ASSERT`` or ``GUARANTEE``. Some repairs are reachable no other way. Every ``drop-*`` ideal in ``examples/`` deletes a guarantee, and for ``amba``, ``full-arbiter``, ``load-balancer``, ``prioritized-arbiter`` and ``round-robin-arbiter`` it is the only ideal there is, so their ``implies_ideal`` score was zero by construction. The operator never deletes the last live guarantee, since a specification with nothing left to guarantee is realisable by doing nothing and is no repair. A deleted guarantee is *tombstoned* in place rather than erased from the list, because the similarity objectives pair requirements by position; erasing one would shift every later requirement and start comparing unrelated pairs against the original. The key value matches ``p_add_assumption``, the two being the same move in opposite directions, and there is no reason for the environment to be easier to strengthen than the system is to relax. The operators are offered as a cascade with early return, ``p_add_assumption`` tested first and this one only where that did not fire, so the realised rate is 0.95 × 0.05 = 0.0475 rather than 0.05. Setting it to 0 reproduces a run from before the operator existed, which is what reproducing a campaign archived before 2026-08-13 requires. Deleting a guarantee is monotonically good for realisability, so the status objective pays for it while the similarity objectives do not, and under NSGA-II a heavily gutted candidate is non-dominated. TLSF sweep D in ``scripts/gen_configs.py`` exists to measure whether that costs repair quality.

An appended assumption's guard may reference output atoms as well as inputs, while its consequent draws from the inputs alone. The ``allow_output_assumptions`` key that governed the guard was removed on 2026-09-11, having defaulted on. The guard buys the reactive-environment assumptions ``G(<output> -> F <input>)`` that an input-only draw cannot express. Stopping the system from writing itself an assumption it can defeat is then the status score's job rather than a syntactic ban's: an assumption side the system can force to fail costs the candidate the top status tier, and the output gate refuses to write it whatever the filters say.

``p_condition_type`` (0.15), ``p_scope`` (0.15) and ``p_monotone`` (0.25) are the three *directional* arms. The first two apply on the FRETISH path alone, and ``p_monotone`` is read by both paths. On the FRETISH path each moves the requirement in a direction fixed before the draw, so a move on any of them strengthens or weakens what it rewrites. The first two are alike: each rewrites a field whose handful of values are ordered by implication, and each is offered beside the other mutation arms. ``p_monotone`` differs on both counts, rewriting a formula field and being offered inside two arms rather than beside them. All three defaulted to 0 on the FRETISH path until 2026-09-11, and moved without any campaign having measured them. The first two now sit at ``p_timing``'s rate, and ``p_monotone`` at the rate the TLSF path already used.

``p_condition_type`` flips a requirement between its *trigger* and *continual* condition type. That order is universal. Continual implies trigger for every scope and every timing: a trigger fires on the rising edges of its condition, and those are a subset of the timepoints at which the condition holds. The two coincide under ``always`` with one of the five plain scopes, and under ``eventually`` with one of the three ``only`` scopes, whose lowering applies the dual wrapper to the negated obligation; everywhere else the order is strict. Switching to continual therefore strengthens the requirement and switching to trigger weakens it, whatever else the requirement says, so the arm needs no per-timing table.

``p_scope`` moves a requirement between FRET *scopes* — ``global``, ``in``, ``except in``, ``before``, ``after``, and the three ``only`` forms — which say over what interval, relative to a mode, the requirement is enforced. Its order is thin and depends on the timing. It was measured with ``ltlfilt --implied-by`` at tick counts 2 and 4, which agree, so it does not depend on the tick count. Under a continual condition at any timing but ``eventually``, ``global`` implies ``in``, ``except in``, ``before`` and ``after``, and ``except in`` implies ``before``; a trigger condition under ``always`` gives that same order, and under any other timing but ``eventually`` it keeps only ``global`` implies ``before`` and ``except in`` implies ``before``.

The order thins out under ``eventually`` because a scope boundary relaxes a bounded obligation and tightens an unbounded one. ``in m ... eventually r`` demands that the response arrive before the mode ends, which the global-scoped form does not, so ``global`` stops implying ``in`` there and the continual cell keeps ``global`` implies ``after`` and ``except in`` implies ``before`` alone. ``except in`` implies ``before`` in every cell, the interval strictly before the first mode entry being contained in the points where the mode does not hold. The three ``only`` scopes are incomparable with every other scope and with each other, so a requirement carrying one cannot move on this field at all.

A non-``global`` scope names a mode, so the arm draws from the specification's declared modes and is inert on a specification that declares none. Every example that predates the feature is global-scoped, which leaves ``p_scope`` a no-op across most of the corpus; ``examples/mode-arbiter`` is the scoped subject that ships with it.

``p_monotone`` is the probability that a field already selected for rewriting takes a *monotone* rewrite in place of the general one. A monotone rewrite returns a formula comparable to what it replaced under implication, where the general rewriter moves the propositional structure freely and leaves the population filters to discard whatever went the wrong way. On the FRETISH path the arm fires inside ``p_response`` and ``p_trigger``, so it is unobservable while both of those sit at 0. The TLSF path offers the same rewrite, ``monotone_rewrite``, as a third arm beside the temporal and propositional ones, with its direction drawn on a fair coin. One key serves both paths since 2026-09-11, and ``[tlsf.mutation] p_monotone``, which the TLSF path read before, is warned about and ignored.

The rewrite offers its whole rule menu on both paths since 2026-09-11, when ``[tlsf.mutation] monotone_atom_rules`` and ``monotone_extra_rules`` were removed; the FRETISH path had always run with both on.

The direction is the requirement's own, as the timing, condition-type and scope arms already take it. Which way the field itself has to move for the requirement to move that way is decided by where the field lands in the lowered formula. ``scope_wrap`` places its argument positively in all eight scope branches, and the continual wrapper ``G(c -> body)`` and the trigger's two conjuncts ``G((!c & Xc) -> X(body)) & (c -> body)`` all place ``body`` positively, so the response takes the requirement's direction unchanged. An ``only`` scope flips it, that lowering applying the dual wrapper to the negation of the timing's obligation and so putting the response under a negation.

Under ``after n ticks`` the response is sat out rather than reversed. ``expand_after`` builds ``!r & X(!r & X(... & X(r)))``, and the bounded form conjoins ``for n (!r)`` with ``within n+1 r``, so the response occurs at both polarities and no rewrite of it says anything about the requirement. The condition is the simpler half: it is uniformly negative under a continual condition type, being the antecedent of ``G(c -> body)``, so it takes the flipped direction, and under a trigger it is sat out too, the rising edge ``(!c & Xc)`` holding it at both polarities at once. ``test/genetic/monotone_arm_tests.cpp`` checks all three entries against a solver rather than against the argument for them, lowering each mutated requirement and asking the implication as an unsatisfiability query.

Each of the three reads its probability before drawing, so at 0 none touches the ``RandomSource`` and the breeding stream is byte-identical to what it was before the arms existed. At the current defaults all three draw, so reproducing a FRETISH run from before 2026-09-11 means writing all three in at 0, as the *config vintage* note in ``experiments/README.md`` records.

Scopes and modes
~~~~~~~~~~~~~~~~

A FRETISH specification declares its modes in a ``modes`` list, beside the input and output atoms:

.. code-block:: json

   {
     "modes": ["maintenance"],
     "in_atoms": ["request"],
     "out_atoms": ["grant"]
   }

The three lists are disjoint, and a scope naming an atom absent from ``modes`` is rejected at load rather than defaulted. Modes are environment-driven: they join ``ltlsynt``'s input side alongside the input atoms. A mode on the output side would let the synthesised system pick its own scope. An ``in``-scoped guarantee is implied by ``G !mode`` and a ``before``-scoped one is discharged by holding the mode at time 0, so every scope would come with a free way of satisfying the requirement without repairing anything. The rejection has the same reason behind it. ``ltlsynt`` is passed only ``--ins`` and treats every atom it was not told about as an output, so an undeclared mode lands silently on the wrong side of the partition.

Mode exclusivity is not generated for you. FRET keeps it in its variable definitions, which the ``formalize`` path never sees, so a specification whose modes are meant to be mutually exclusive has to say so as an ordinary requirement, marked non-weakenable so that the search cannot relax it. ``examples/fsm-combined/spec.json`` already does this for its enum atoms. Writing the axiom by hand is the price of a lowering that never invents a constraint the author did not write.

The scope reaches the fitness as well: ``syntactic_similarity`` averages five components rather than three, the trigger, response and timing components plus a scope one and a condition-type one, so the search has a gradient on both of the fields ``p_scope`` and ``p_condition_type`` move. The scope component is the *Jaccard* overlap of the timepoint regions the two scopes enforce over — relative to one mode a trace divides into the prefix before the mode first holds, the points where it holds, the gaps between mode intervals, and the suffix after it last holds — averaged with whether the two scopes name the same mode, and deliberately not the implication order above, which depends on the timing as well, where a similarity between two scopes has to be a property of the scopes alone. An ``only`` scope's regions are tagged apart from a plain scope's, so ``except in m`` and ``only in m`` share every region and still earn no region credit, the two making opposite claims about the same timepoints. The condition-type component is the same overlap over the two-element order above, Continual implying Trigger, so it takes the value 1 or 1/2. Both components change what the search finds even on a corpus that uses no scopes, each per-index component dividing by the larger of the two requirement counts, so a candidate that gained an assumption or tombstoned a guarantee scores below 1 on both. Over seeded runs of ``fsm``, ``fsm-timing`` and ``fsm-combined`` at five seeds each, 3 generations at population 40, 12 of 15 returned different repairs from the three-component build, two of them one repair more and none fewer, which is far too few runs to read as a quality result.

Termination
-----------

``genetic.termination`` chooses what the search budget is counted in. PEREDUR's budget is ``generations`` rounds of evolution, where AuRUS's is a count of individuals, so neither tool can be given the other's budget without being able to count the same thing. The head-to-head campaign ``experiments/2026-08-14-aurus-h2h`` gave PEREDUR 10 generations at population 200 and AuRUS ``-Max=1000``, which are not the same budget in any currency.

.. list-table::
   :header-rows: 1
   :widths: 32 14 54

   * - Key
     - Default
     - Meaning
   * - ``genetic.termination``
     - ``"generations"``
     - Budget currency: ``"generations"`` or ``"individuals"``
   * - ``genetic.max_individuals``
     - 0
     - Individual budget, read under ``"individuals"`` alone
   * - ``genetic.max_wall_s``
     - 0 (off)
     - Wall-clock deadline in seconds, honoured under both modes

AuRUS's genetic algorithm stops once ``numberOfVisitedIndividuals`` reaches ``GA_MAX_NUM_INDIVIDUALS``, which is 1000 in the drivers that produced its published numbers. It counts one individual per offspring admitted to the new population, and checks between offspring rather than at a generation boundary. ``termination = "individuals"`` therefore requires ``max_individuals >= 1``; zero is rejected rather than read as unlimited, a run with no search budget being what the other mode is for. ``generations`` still bounds a run under ``"individuals"``, as ``GA_GENERATIONS`` does for AuRUS.

An offspring counts only when it differs from the parent it was bred from. That matches AuRUS, whose mutation arm increments only on ``!chromosome.equals(mutated)`` and whose crossover arm increments only for an offspring distinct from the first parent. A slot whose crossover and mutation draws both declined is free on both sides.

The budget is checked between offspring inside the breeding loop, and again before each generation. Checking at the generation boundary alone would overshoot by up to one generation's offspring, which is 20% of a 1000-individual cap at the shipping population size. Breeding returns a short offspring set when the budget runs out part-way, and the rest of that generation — filters, scoring, selection — completes normally, which is also what AuRUS does after it breaks out of its own loops.

``max_wall_s`` is independent of the mode. A run killed by the harness's external ``timeout`` writes no ``run.json`` at all, so it is *censored* with nothing recorded, where a run that stops on its own deadline writes its repairs and its manifest and says which budget ended it. That is what a survival analysis such as time-to-first-repair needs, since a censored observation carrying no data is not usable. The deadline is measured from the same origin as the manifest's own ``wall_s``. Filters, scoring and the final realisability gate are not interrupted, so a run overruns its deadline by whatever the generation it was in had left.

The manifest gained three fields at schema version 21: ``stopped_by``, reading ``"generations"``, ``"individuals"`` or ``"deadline"``; ``generations_run``; and ``individuals_bred``. The last is null under the default configuration, because counting an offspring means comparing it against its parent and an unbudgeted run does not pay for that, so a zero would read as a fact rather than as an absence. A run that trips both budgets reports individuals ahead of a deadline, matching the order AuRUS's ``checkTermination()`` tests them in.

Schema version 23 adds an ``implication`` block, holding ``comparisons``, ``skipped``, ``duplicates``, ``timeouts`` and ``equivalent_collapsed``. These counters reached no manifest field before, so they printed to the diagnostics report alone and a campaign reading ``run.json`` could not see them at all. ``comparisons`` is the denominator the other four are read against: ``skipped`` are the pairs an earlier verdict already settled, ``duplicates`` the specifications excluded from the sweep outright, and ``timeouts`` the checks that returned no verdict. ``equivalent_collapsed`` counts the pairs found mutually equivalent, so a class of k members contributes k-1. A timed-out check reads as a non-implication, which makes ``timeouts`` the bound on how much equivalence the sweep may have missed.

At the defaults the budget cannot fire, and breeding skips both the parent comparison and the clock read when it cannot, so the ``RandomSource`` draw stream is byte-identical to what it was before these keys existed and a seeded run reproduces. The determinism goldens are unchanged. No entry is owed in the *config vintage* note in ``experiments/README.md`` either: an archived config omitting these keys means exactly what it always meant.

One caveat sits on ``max_wall_s``. A deadline makes a run's output depend on the machine it ran on, so two runs of one seed can legitimately differ under it — the same property the per-tool budgets in ``[runtime]`` already have.

TLSF mode
---------

``[tlsf]`` applies on the TLSF path alone and is ignored on the FRETISH one, except for ``repair_mode``, ``muc_max_iterations`` and ``muc_screen_depth``, which both paths read.

``repair_mode`` chooses the repair strategy. **monolithic** (the default) evolves the whole specification at once. **muc** instead extracts a *minimal unrealisable core* and evolves only that sub-specification. It then reintegrates the repaired core with the untouched non-core guarantees, and repeats on the recombined specification until it is realisable. ``muc_max_iterations`` (32) caps that outer loop, so a specification whose core never becomes realisable ends the run without a repair rather than looping forever. :doc:`tlsf` describes the mode and the ``mucs`` tool that exposes the same extraction on its own.

On FRETISH input the core is a set of guarantees, held against every assumption and mode. ``muc_screen_depth`` (3) is the largest guarantee subset the extraction asks about directly: every subset of that size or smaller is checked concurrently, smallest first, and the lowest-ranked unrealisable one is the core. QuickXplain walks the whole specification only when no subset that small conflicts. The screen costs one synthesis query per subset, so it grows as ``n^k``: on the 114-guarantee ventilator the pairs took 11s and the triples 433s. A large specification whose cheap conflicts have been repaired may leave the whole-specification query undecided within ``runtime.ltlsynt_timeout_ms``. The loop then screens the small subsets alone and stops when none conflicts. A reintegrated specification whose gate query goes undecided, but whose every subset of up to ``muc_screen_depth`` guarantees is decided realisable, is written as ``provisional_N.json``. It is never a repair: ``n_repairs`` does not count it, and neither the accumulator nor the implication filter sees it. ``run.json`` records ``n_provisional`` and ``n_gate_undecided``, the gate checks whose whole-specification query never answered. In this mode ``genetic.max_wall_s`` bounds the whole run, not only the search. Once it passes, the core extraction, the gate and the screens start no new query, so the run reaches its normal exit and writes ``run.json`` and its repairs. A reintegrated specification left without a gate verdict or a finished screen counts as undecided, is never provisional, and is counted in ``n_deadline_unscreened``. The gate and the screens take candidates best first by the core's fitness, so the deadline cuts the least fit. A query already running when the deadline passes finishes within its own tool timeout.

``[tlsf.mutation]`` tunes the TLSF operators. ``p_assumption`` (0.3) is the probability of mutating an assumption-side section (``INITIALLY``, ``REQUIRE``, ``ASSUME``) rather than a guarantee-side one (``PRESET``, ``ASSERT``, ``GUARANTEE``); the guarantee side takes the complement. ``p_temporal`` (0.2) is the probability that a chosen formula gets the temporal-structure mutation — inserting, dropping or swapping ``X``/``F``/``G``/``U``/``R``/``W`` — rather than the skeleton-preserving propositional rewrite. At ``p_temporal = 0`` the temporal skeleton of an existing formula is never altered.

Filters
-------

``[filters]`` holds one key, ``run_implication``, a final screen over the realizable survivors. The *vacuity* stage has no key: it runs once per generation, before scoring, so a candidate it drops never costs a model-count or a synthesis query.

``run_implication`` keeps only the specifications that are maximal under the implication partial order, discarding any repair another repair already subsumes. Mutual implication subsumes one side as well: two specifications that imply each other are one repair written two ways, so the survivor is whichever has the higher syntactic similarity to the original, and ``Specification::operator<`` decides where similarity ties. That tie-break has to be a total order because the pairwise checks run concurrently, and keeping whichever pair finished first would not reproduce under a fixed seed. The collapse adds no solver call, both implication directions having been computed already, and is best-effort: a timed-out check reads as non-implication, so an equivalence that neither direction proves in time keeps both sides. It is a final screen rather than a per-generation filter, and it is on by default. Its implication check is the same on both paths since 2026-09-11: ``spec_implies`` and ``tlsf_spec_implies`` each lower the whole specification to one LTL formula and ask ``black`` whether ``(original) & !(candidate)`` is unsatisfiable. The FRETISH check decomposed the assume-guarantee pair until then, matching each requirement against a *single* counterpart, so it missed an implication holding only via several requirements together. Archived FRETISH ``n_repairs`` and ``n_implies`` were recorded under that weaker check and do not compare against rows taken after it, the first falling 17.8% over 25 paired runs. The vacuity stage drops candidates that hold for free rather than because anything was repaired, and is the weaker relative of the well-separation check below. It runs three tests in one ``vacuity`` stage, applied cheapest first.

A **syntactic screen** rejects a requirement whose condition is the literal ``false``, in an assumption or a guarantee. It costs no solver call, and the condition sits only in the antecedent of the lowered implication, so it is vacuous under every timing. There is deliberately no dual over responses: a ``true`` response is a no-op under most timings but not under ``after n ticks``, whose lowering negates the response and so makes ``true`` the strongest guarantee expressible. Only the check below reads the lowered formula, so only it gets that case right. On TLSF, which has no condition/response split and so no such lowering, the screen reads a ``false`` formula in INITIALLY/REQUIRE/ASSUME or a ``true`` formula in PRESET/ASSERT/GUARANTEE.

A **guarantee validity check** then negates each guarantee individually and rejects the candidate on the first one whose negation is unsatisfiable — a guarantee that demands nothing. Per guarantee rather than over the guarantee conjunction: the conjunction is valid only when *every* guarantee is, so it would keep a candidate that guts one conjunct while the rest still constrain the system.

An **assumption satisfiability check** asks whether the assumptions are *jointly unsatisfiable*: a false antecedent makes ``(assumptions) -> (guarantees)`` a tautology, so such a candidate is realizable for free and is not a repair. This one stays a single joint query. Satisfiability does not distribute over conjunction: ``G p`` and ``G !p`` are each satisfiable and jointly are not.

The guarantee queries are cached per requirement, so a guarantee carried over from a parent — and every locked one — costs nothing after its first evaluation; the assumption query is cached per assumption side, and costs nothing at all on a specification with no assumptions. Every solver verdict is conservative in the same direction: a timeout reads as non-vacuous, so no candidate is dropped on a non-answer.

It runs in every generation of every run, the ``run_vacuity`` key that could switch it off having been removed on 2026-09-11. Collecting the realizable survivors applies every row of the shared correctness table (``include/filter/correctness.hpp``) as well, on both paths, so the per-generation stage adds search pressure and the gate is what holds the output to the property.

A ``run_weakening`` final screen, which kept only repairs the original logically implies, was removed on 2026-09-11. It had defaulted off since 2026-08-20, when ``experiments/2026-08-20-ops-weakening`` found that turning it off gained ``implies_ideal`` on 38 of 720 matched runs and lost it on none.

*Well-separation* has no key. A candidate is not well-separated when the system can satisfy it by forcing its own assumptions to fail. Under ``"tiered"`` and ``"mrs"`` the status objective scores such a candidate 0.5 during the search, and the gate over the collected survivors refuses to write it under every grading. The per-generation ``run_well_separation`` filter was removed on 2026-09-11, having defaulted off since ``b101ada``: filters run before scoring, so with it on an ill-separated candidate was dropped before the 0.5 tier could fire.

The gate reads a failed synthesis the other way round from every other caller. Everywhere else an undecided ``ltlsynt`` query means "do not admit this repair", so falling back on *unrealizable* is the cautious answer; here unrealizable is what admits a candidate, so an undecided query reads as not well-separated and the candidate is refused. The undecided outcome is memoised, so ``ltlsynt`` is not asked again for the rest of the run.

The per-filter run intervals that once throttled the filters were removed: across every archived campaign not one config had ever set them.

Runtime
-------

``runtime.parallel`` overrides the thread pool size, which otherwise follows ``available_parallelism()``: the hardware concurrency narrowed by the process's CPU affinity mask and by the cgroup CPU quota, where either applies. ``std::thread::hardware_concurrency()`` alone reports the host's online CPUs and sees neither, so a container given four CPUs on a 64-core host would size the pool at 64 and oversubscribe its quota sixteen-fold.

No key caps ``ltlsynt`` concurrency separately. ``runtime.max_concurrent_realizability`` did until 2026-09-11, defaulting to unlimited; peak ``ltlsynt`` memory is now bounded by ``parallel``, and the TLSF campaigns run one PEREDUR process per machine at ``--jobs 1``.

The cap was added on the premise that ``ltlsynt`` is *the* memory hog, and measurement qualified that. Over 149,153 tool invocations across the corpus, ``ltlsynt`` peaked at 260 MB, against 1.8 GB for ``ltl2tgba`` and 3.4 GB for ``ltlfilt`` — which is 51% of all invocations and has by far the worst tail. On the calls that sample captured, ``ltlfilt`` is the hog and ``ltlsynt`` is not.

What that sample cannot do is bound ``ltlsynt``. It is censored precisely where the blowups live: six of the heaviest runs (``amba``, ``full-arbiter``, ``prioritized-arbiter``) were killed on a 600 s wall-clock cap, skipped the ``atexit`` report and wrote no profile at all. Rare multi-gigabyte ``ltlsynt`` calls do happen, and none of them are in those figures. Treat 260 MB as what the common case costs, not as a ceiling. The maximum is censored, and on one example the tail moved 26x between two seeds.

So the cap guarded a rare tail rather than the typical call, which is why it defaulted to unlimited and why it was removed rather than tuned. On a memory-constrained machine, or on a corpus with the heavy SYNTCOMP arbiters in it, lower ``parallel`` instead. Bounding ``ltlfilt`` concurrency is the lever for the *common* case, and there is no key for it yet.

``runtime.max_scoring_failure_rate`` (0.15) bounds what fraction of a generation may fail to score before the run aborts. A fitness function that throws — in practice an external tool failing on one evolved formula — costs that individual rather than the whole run. The search is stochastic, so one lost candidate out of a population is noise, whereas aborting at generation 23 of 40 loses everything. Above this fraction the tooling is broken rather than the formula, and continuing would only evolve noise into the output.

Per-tool budgets
~~~~~~~~~~~~~~~~

Each external tool has a per-call wall-clock budget in milliseconds. A budget of 0 means no timeout.

.. list-table::
   :header-rows: 1
   :widths: 34 12 54

   * - Key
     - Default
     - On expiry
   * - ``runtime.black_timeout_ms``
     - 1000
     - The satisfiability query is undecided.
   * - ``runtime.ltlfilt_timeout_ms``
     - 10000
     - The formula goes unsimplified.
   * - ``runtime.ltlsynt_timeout_ms``
     - 500
     - The realisability query is undecided.
   * - ``runtime.ltl2tgba_timeout_ms``
     - 60000
     - The individual is dropped.

What decides whether a budget defaults on is the cost of abandoning a call weighed against the cost of not doing so. ``black`` has its own internal timeout, so bounding it is routine. ``ltlfilt`` is bounded because ``--simplify`` is super-exponential on the deep nested-``X`` conjunctions the search builds, and an abandoned call there costs only a missed simplification, never a candidate.

``ltlsynt`` and ``ltl2tgba`` are bounded despite an abandoned call costing a candidate, because an unbounded one can cost the whole run: ``ltlsynt`` occasionally runs for minutes with no upper limit, and ``ltl2tgba``'s deterministic construction has the same super-exponential blowup as ``ltlfilt``, running for hours and leaking orphaned multi-gigabyte processes over a long campaign. Losing one candidate out of a population is the noise ``max_scoring_failure_rate`` already exists to absorb; losing the run at generation 23 of 40 is not. Both defaults are what every archived TLSF campaign set explicitly, which is the other half of the argument — the shipped default should be the configuration real runs use.

The two budgets differ by two orders of magnitude because the tools' distributions do. ``ltlsynt`` call durations are sharply bimodal: 95% of a TLSF campaign's calls finished under 50 ms, the 0.5–1 s band was almost empty, and a 500 ms cap abandoned within 0.1% of the calls a 10 s cap did — so a query that blows 500 ms is in the minutes-long tail rather than near the boundary. A legitimate ``ltl2tgba`` determinisation, by contrast, can genuinely take tens of seconds, so its budget is set to bound the blowup rather than to discriminate a tail.

``ganak`` has no budget, its ``runtime.ganak_timeout_ms`` key having been removed on 2026-09-11 at its default of no timeout. Counting is the fitness function's real work, so a slow count is usually a legitimately hard one rather than a blowup, and there is no equivalent runaway to bound. ``ganak`` is also rarely called now, ``count_guard_models`` counting the guards it once passed on in process.

.. note::

   A wall-clock budget makes a run's output depend on the machine it ran on. A slower host abandons a call that a faster one completes, so the same ``--seed`` can give different repairs on different hardware — and a loaded machine differs from an idle one. The seed fixes the search; the budgets do not. When reproducing a result exactly, match the hardware, and read the ``tool_calls`` timeout counts in ``<output-dir>/run.json``, which every run that completes writes regardless of any reporting flag, to tell a genuine difference from an abandoned call.

An undecided ``ltlsynt`` answer is resolved by each caller in its own direction — no repair is admitted on one, and the well-separation check at the gate refuses the candidate, for the reason given under `Filters`_. A dropped individual counts against ``max_scoring_failure_rate``.

Reporting
~~~~~~~~~

``runtime.dashboard`` streams per-stage and per-generation progress to ``<output-dir>/progress.jsonl`` and copies the live dashboard page beside it, so ``python3 -m http.server -d <output-dir> 8000`` shows the run as it happens. It defaults to false, because a campaign of many runs pays for the extra file and its flushes with nobody watching; ``peredur --dashboard`` enables it for a single run without editing a config. The flag can only turn the dashboard on — a config that already asked for it is not disabled by omitting the flag.

``peredur --cpu-report`` prints a CPU-attribution report at the end, separating time spent in PEREDUR's own code from time spent in the external tools (``black``, ``ltlsynt``, ``ganak``), measured per-process via ``getrusage``/``wait4``. It is a flag rather than a config key because it asks about one interactive run rather than about the search, as ``PEREDUR_PROFILE`` does for the scope profiler.

``peredur --diagnostics`` prints the engine-internal counters at exit: the per-tool call, time and cache table, the *constant-folded* count — satisfiability questions ``ltlfilt`` decided outright, with no ``black`` call — the ``ltl2tgba`` tautology-substitution count, printed only when it is non-zero, which counts the formulae the counting path replaced with the universal automaton to work around a SPOT bug that exits 2 on a tautology, and the fitness cache hit rate. Every one of them printed on every run until they were put behind this flag. They are off by default because stdout is for watching a run in progress, and none of them says anything about the run's repairs. Nothing is lost by that default: every figure in the report is written to ``<output-dir>/run.json`` whether or not the flag is given, and a campaign should read them from there, since parsing a table out of a log is worse than reading the JSON. It is a flag rather than a config key for the same reason ``--cpu-report`` is — it asks about one interactive run rather than about the search.

Left to itself, stdout carries the run rather than a report: a status line per generation, then the filter report, the scoring report and a closing ``Done in <n>s``. Keeping it to that is also why ``peredur`` rejects an argument it does not recognise instead of ignoring it. The ones that catch people name real config keys — ``--generations`` and ``--population_size`` are settings rather than flags, and passing them on the command line used to run an entire search against the defaults without a word about it.

The run manifest
~~~~~~~~~~~~~~~~

Every run that completes writes a *run manifest* to ``<output-dir>/run.json``, so an output directory names its own inputs. Nothing in these docs described it until now, while two passages above already sent the reader off to read it.

It records ``schema_version`` (3 at present), ``tool``, the build's ``commit``, ``commit_short`` and ``dirty``, then ``finished_utc``, ``wall_s``, ``input``, ``seed``, ``n_repairs`` and ``input_screen``. That last one is null when the input specification passed every correctness check at load time, and otherwise names the check it failed. ``config`` holds the fully merged configuration in TOML section layout, so it diffs directly against a config file. ``tool_calls`` holds ``calls``, ``cache_hits``, ``timeouts`` and ``total_s`` per tool, except that ``ganak`` has no ``timeouts``, running untimed. The engine counters follow: ``n_constant_folded``, ``n_tautology_substitutions``, and ``fitness_cache``, itself a pair of ``hits`` and ``misses``.

``schema_version`` went from 1 to 2 when those last three were added, at the point the counters stopped printing to stdout by default and the manifest became their only unconditional record. The rule that falls out of that is worth stating: a counter added to the ``--diagnostics`` report has to be added to the manifest as well, or turning the flag off loses it. A figure that reaches stdout alone is a figure the next default can retire without anyone noticing.

2 to 3 added ``input_screen``, and the reason is the same one in a different guise. The load-time screen warns on stdout, so a campaign that wanted to know which of its runs started from an ill-formed input had to grep the log for that warning; the manifest field lets it partition the runs on a key it already reads. Both bumps say that anything a run learns about itself belongs in the file, not only in the output stream.
