fitness/status.hpp
==================

``status_score`` scores a candidate on a three-point scale by running ``black`` on each of its component formulae and, once all of them hold, ``ltlsynt`` on the whole specification:

.. list-table::
   :header-rows: 1

   * - Score
     - Meaning
   * - 1.0
     - Realisable
   * - 0.5
     - Every component satisfiable, but the specification is unrealisable
   * - 0.0
     - Some component formula is unsatisfiable on its own

Both front ends score on this scale, and differ only in how a candidate decomposes into components and in how realisability is queried. ``specification_status`` uses the per-requirement conjunction ``condition & response``; ``tlsf_status`` uses the individual formulae of the six TLSF sections.

The scale was previously five-point, grading the region below realisability by whether the guarantee side or the assumption side was jointly unsatisfiable. Instrumenting both front ends over 887k scored candidates put under 0.32% of the population into those tiers, and both were removed --- for different reasons. The guarantee-side tier was measured: nothing filters a jointly unsatisfiable guarantee side, and it fired on 46 candidates out of 15,115 on the TLSF path, all on one specification. The assumption-side tier never fired at all, and could not have. Its predicate is exactly the satisfiability half of the vacuity filter's, and candidate filters run on offspring *before* those offspring are scored, so such a candidate never reaches the objective. That tier was unreachable whenever the vacuity stage ran, which it did by default and, since its ``run_vacuity`` key was removed on 2026-09-11, does in every run.

Ranking *within* unrealisability needs a measure of how far a candidate is from realisable, which a satisfiability query cannot express.

``status_score_aurus`` is the exception, and is not an improvement on the scale above but a reproduction of AuRUS's own: five levels below realisability, graded by which side of the candidate is satisfiable on its own, so an ablation can cross PEREDUR's grading against the design it derives from. It is selected by ``fitness.status_grading = "aurus"``, it has no per-component tier, and it is the one scorer here that does not fold well-separation into its realisability query — AuRUS never asks that question, and an arm that asked it would measure something else.

Weighted equally with the semantic similarity component so realisable candidates are strongly preferred throughout evolution.

.. doxygenfile:: status.hpp
