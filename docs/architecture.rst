Architecture
============

PEREDUR is a C++17 tool that repairs *unrealisable* reactive specifications with a *genetic algorithm*. A specification is unrealisable when no implementation satisfies it against every environment; ``ltlsynt`` reports that, but not what to change. PEREDUR searches for the edits that make it realisable while staying close to what was written.

Two input languages are accepted. `FRET <https://nasa.github.io/fret/>`_ requirements arrive as *JavaScript Object Notation* (JSON), and basic *Temporal Logic Synthesis Format* (TLSF) specifications arrive as ``.tlsf``. The format is taken from the file extension, or forced with ``--format fretish`` / ``--format tlsf``. Both paths share the genetic core: the same pipeline, selection schemes, fitness components and filters. What differs is what a candidate is, and how it is mutated, scored and written back. :doc:`tlsf` covers the TLSF path in detail.

Algorithm flow
--------------

1. **Load** a specification from ``--input``. On the FRETISH path this is a :class:`Specification`: lists of assumption and guarantee :class:`Requirement` objects, each carrying a condition formula, a response formula, a :type:`Timing` modifier, and a derived *linear temporal logic* (LTL) string. On the TLSF path it is a ``tlsf::Specification`` holding the six specification sections.

2. **Build fitness** — an :class:`AggregateWeightedFitnessFunction` combining three weighted components: semantic similarity, realisability status, and syntactic similarity. The per-generation :class:`FilterFunction` list is constructed from the original spec at the same time. See :doc:`configuration` for the weights.

3. **Seed** a *random number generator* (RNG) from ``--seed`` or ``std::random_device``, and register crash metadata so a signal handler can record the seed in the crash log.

4. **Evolve** for ``Config::generations`` rounds, each round running the generation pipeline described below.

5. **Collect** the realisable survivors from the final population, re-checked with ``black`` + ``ltlsynt``.

6. **Screen** those survivors: deduplicate, then apply the optional implication filter (keep only the maximal specs under the implication partial order).

7. **Score, sort, and write** each surviving spec to the output directory — ``repair_N.json`` on the FRETISH path, ``repair_N.tlsf`` on the TLSF path, each paired with a ``repair_N.fitness.json`` holding its score.

Step 4 has a second mode on both paths. ``[tlsf] repair_mode = "muc"`` replaces the single whole-spec search with a loop that extracts a *minimal unrealisable core*, evolves only that sub-specification, and reintegrates the repaired core with the untouched non-core guarantees. :doc:`tlsf` describes it.

The generation pipeline
-----------------------

``make_generation_pipeline`` (``include/genetic/pipeline.hpp``) returns an ordered vector of named stages, which ``run_generation_pipeline`` executes and reports to an optional ``StageObserver``. The live dashboard derives its stage list from those reports, so a new stage appears there with no change to the page.

.. list-table::
   :header-rows: 1
   :widths: 25 75

   * - Stage
     - Effect
   * - ``order-parents``
     - Rank the population and take the top slice as the breeding pool.
   * - ``breed``
     - Crossover and mutation, interleaved per offspring slot.
   * - *filter stages*
     - One named stage per active filter; see below.
   * - ``filter-fallback``
     - If the chain emptied the offspring, re-apply the correctness filters alone to the unfiltered offspring.
   * - ``restore-elites``
     - Carry the previous generation's elites through unchanged.
   * - ``pad``
     - Top the population back up to size.
   * - ``score``
     - Evaluate the fitness components in a thread pool.
   * - ``select``
     - Pool parents with offspring and choose the survivors.

Filters run *before* scoring, so a dropped candidate never costs a model-count or a synthesis query, and a filter's own solver calls warm the caches that scoring then hits. A stage that re-tests a filter's predicate on a population that filter already judged is therefore dead code rather than a safety net.

The filters run in the order ``dedup``, ``bloat-cap``, then ``vacuity``. The same list on both paths. The ``vacuity`` stage is built from the shared correctness table — ``correctness_checks`` in ``include/filter/correctness.hpp``, instantiated for each front end — one stage per row whose ``per_generation`` bool is set. Since 2026-09-11 that bool is fixed on the row rather than read from a config flag, and the ``not-well-separated`` row sets it false, so it builds no stage. Step 5 applies every row of that same table to every survivor it collects, whatever its ``per_generation`` says, so a correctness property cannot be enforced during the search and then dropped at the output. Each filter is tagged with a ``FilterKind`` (``include/genetic/generation.hpp``). ``filter-fallback`` re-applies only the ``Correctness`` filters, and only to offspring those filters never saw, because ``dedup`` and ``bloat-cap`` run first and shadow them. It re-admits candidates dropped as duplicates or as oversized, and none of those dropped as unfit to breed from. The default kind is ``Correctness``, so a filter added without a tag costs a wasted re-test rather than a re-admitted bad candidate.

A final weakening screen at step 6 was removed on 2026-09-11, having defaulted off since 2026-08-20; :doc:`configuration` gives the measurement behind that.

Breeding is a single stage by design. Crossover and mutation interleave per offspring slot, so splitting them would reorder every RNG draw after the first and break seed reproducibility. The ``determinism`` test suite pins the draw stream against exactly that.

Selection
---------

``stage_select`` follows ``Config::selection_scheme``. Both NSGA-II schemes rank candidates by non-dominated sorting and crowding distance over the individual objectives (``include/genetic/nsga2.hpp``) and pool parents with offspring. ``Nsga2Truncate`` truncates that pool at ``population_size``. The default, ``Nsga2Apportion``, deduplicates the pool first and apportions the slots over the distinct survivors. ``WeightedAverage`` ranks by the aggregate scalar instead; it converges prematurely on the FRETISH corpus, and on TLSF it trades repair quality for yield — see :doc:`configuration`.

Every scheme carries both the per-objective vector and the weighted scalar on each ``Scored<Spec>``, so outputs stay comparable across runs. :doc:`configuration` gives the measured differences between the three.

Key types
---------

``Timing``
  ``std::variant<Immediately, NextTimepoint, WithinTicks, ForTicks, AfterTicks, Eventually, Always, Until, Before>``.  Encodes FRETISH temporal operators; ``Until`` and ``Before`` carry a propositional stop condition.  Defined in ``requirement.hpp``; constructors live in the ``timing::`` namespace so *argument-dependent lookup* (ADL) finds them for variant visitors.

``ConditionType``
  ``enum class { Trigger, Continual }``.  Controls whether a :class:`Requirement` fires on a rising edge of its condition (``Trigger``) or at every timepoint where the condition holds (``Continual``).

``Requirement``
  The unit of repair on the FRETISH path.  Holds ``m_condition``, ``m_response``, ``m_timing``, ``m_condition_type``, the derived ``m_ltl`` string produced by ``requirement_to_ltl``, and ``m_weakenable``.  When ``m_weakenable`` is false the requirement is locked: the genetic algorithm never mutates it, never uses it as a crossover source, and never simplifies it.  The flag is part of the requirement's identity, so it participates in ``operator<``, ``operator==`` and hashing.  It serialises as the optional JSON key ``weakenable``, which defaults to true and is emitted only when false.

``Specification``
  A list of assumption :class:`Requirement` objects, a list of guarantee :class:`Requirement` objects, and the input/output atom universes used by crossover and mutation.

``tlsf::Specification``
  The TLSF counterpart: the input and output signal sets, and the six sections ``INITIALLY``, ``PRESET``, ``REQUIRE``, ``ASSUME``, ``ASSERT`` and ``GUARANTEE``.  Its operators work over whole LTL formulae within a section rather than over condition/response pairs.  See ``tlsf/specification.hpp``.

``Formula``
  A propositional *abstract syntax tree* (AST), held behind a pointer to implementation, supporting parse, simplify, DIMACS conversion, syntactic similarity, and post-order rewriting.  See ``prop_formula.hpp``.

``TransferSystem``
  A finite-state automaton (states, valuation-count vector, transition matrix) built from an LTL formula via ``ltl2tgba``/Ganak.  Used by the semantic similarity and model-counting subsystems to count satisfying bounded traces via matrix exponentiation.

``Count``
  ``long double``.  Trace counts are only consumed as ratios cast to ``double``, so exponent range matters more than exact integer width.  All arithmetic must go through ``count_add_overflow`` / ``count_mul_overflow`` with an assert on the overflow flag; overflow means the result went non-finite.

Module layout
-------------

``include/`` is the published *application programming interface* (API) surface, and every header in it has a page in this reference. Implementation detail lives under ``src/`` and is deliberately not published.

.. code-block:: text

   include/
     bounded_async.hpp   — bounded-concurrency async dispatch
     config.hpp          — algorithm parameters and their defaults
     config_io.hpp       — TOML config parsing and key validation
     dashboard.hpp       — progress.jsonl observer for the live dashboard
     profile.hpp         — scope profiler (PEREDUR_PROFILE)
     prop_formula.hpp    — propositional formula AST (Formula)
     requirement.hpp     — Timing, ConditionType, Requirement, Specification
     serialisation.hpp   — JSON serialisation for all core types
     status_line.hpp     — terminal progress reporting
     thread_pool.hpp     — fixed-size worker thread pool
     version.hpp         — the commit a binary was built from
     crash/              — crash_handler: SIGSEGV/SIGABRT/SIGFPE reporter
     filter/             — bloat cap, implication, implication check, vacuity, well-separation
     fitness/            — syntactic/semantic similarity, status, model counter
     genetic/            — crossover, mutation, generation, pipeline, NSGA-II, scored, random source
     runner/             — wrappers for black, ganak, ltl2tgba/ltlsynt, ltlfilt, the FRET formaliser
     tlsf/               — parser, writer, specification, and the TLSF operator set

Binaries
--------

``peredur``
  The repair driver.  Reads a specification, runs the search, and writes the ranked repairs.

``realize``
  Reports ``REALIZABLE`` or ``UNREALIZABLE`` for each input specification.

``ltl``
  Prints the LTL a specification translates to, per requirement or per section, and the combined lowering.

``compare``
  Compares a directory of repairs against a directory of known-ideal ones, under the assume-guarantee implication order.

``mucs``
  Extracts a minimal unrealisable core from a TLSF specification.  TLSF only.

``maximal``
  Reports which specifications in a set are maximal under the implication order, then quotients the survivors by mutual implication.  Takes any directory of specification files, so a foreign tool's output can be measured on the definition ``peredur`` applies to its own.  Both front ends, selected by the extension present as ``compare`` selects, since a maximality curve over a FRETISH campaign reads the same accumulated candidates a TLSF one does.  The two oracles ask the same question, each lowering a whole specification to one formula and asking a complete query, so a count from one format is comparable with a count from the other.

``signal_tracer`` is an internal helper rather than a user-facing tool: the crash handler runs it out-of-process to symbolise a stack trace, because unwinding in-process from a signal handler is not safe. Run any of the others with ``--help`` for its options, or with ``--version`` for the commit it was built from.

External tools
--------------

``ltl2tgba``, ``ltlsynt``
  From the SPOT library.  Built from source via ``cmake/spot.cmake``; located via the ``SPOT_BIN_DIR`` compile macro.  Used for automaton construction and LTL realisability checking.

``ltlfilt``
  Also from SPOT.  Simplifies and canonicalises LTL formulae behind ``simplify_ltl``, one exec per cache miss.

``black``
  LTL satisfiability checker (``black-sat``).  Found on ``PATH`` or downloaded via ``cmake/black.cmake``; path passed as ``BLACK_EXECUTABLE_PATH``.  Used for the status fitness component and the implication-based filters.

``ganak``
  Weighted model counter.  Downloaded as a release binary via ``cmake/ganak.cmake``; path passed as ``GANAK_EXECUTABLE_PATH``.  Used to count satisfying valuations for each automaton transition, which is what enables trace-level model counting via the ``TransferSystem`` matrix.

``node``
  Runs the vendored FRET formaliser CLI, which lowers FRETISH requirements to LTL.  Looked up on ``PATH`` at run time rather than built or fetched by CMake, so it must be installed separately.  Unlike the others it is a persistent bidirectional child process rather than one exec per query, and it serves the FRETISH path alone.

Every pipe a runner opens is created with ``O_CLOEXEC``. These runners are called from many scoring-pool threads at once, so a fork on one thread would otherwise inherit the pipes every other in-flight call holds open, and keep them alive past its own exec — leaving the reader waiting on an end-of-file that never arrives.

The architecture keeps returning to one constraint: every interesting question about a specification is answered by an external process, and each answer is expensive. Filtering before scoring, warming the caches that scoring hits, and holding the pipes right across a fork are the same concern surfacing at three different points in the pipeline.
