genetic/diversity.hpp
=====================

The FRETISH mechanisms that spread the search over unlike repairs, each behind a key that is off by default (see "Diversity mechanisms" in ``docs/dev/algorithm.md``), and the counters each writes to ``run.json``.

``specification_distance``
  The share of assumption and guarantee slots in which two specifications hold different requirements, pairing slots by index. ``genetic.crowding = "specification"`` orders each NSGA-II front by farthest-point selection under it (``farthest_point_scores`` in ``nsga2.hpp``).

``DiversityState``
  The run-scoped state the diversity keys keep across generations, lent to each generation by the generation loop: the accumulator M, and for ``genetic.novelty`` a per-specification count of the members of M that rule-imply it, extended as M grows.

``novelty_objective``
  The extra objective ``genetic.novelty`` ranks by: minus that count for a realizable candidate, 0 for an unrealizable one.
  ``genetic.novelty_tiebreak`` reads the same values as the key that orders each front before crowding (``Nsga2Options::tiebreak`` in ``nsga2.hpp``).

``NoveltyTiebreakStats``
  What ``genetic.novelty_tiebreak`` did: the sorts it ordered, the values it read, and how often the order or the survivors differed from crowding alone.

``SlotCoverage``
  For ``mutation.slot_coverage``, how many members of M edit each slot of the original. ``DiversityState::slot_coverage`` brings it up to date once per generation, and mutation weights its slot draw by ``1 / (1 + c)``.

``spread_initial_population``
  The seed population of ``genetic.spread_init``: the original plus one-step weakenings spread round-robin over its refused guarantees and its weakenable assumptions, each proved Weaker by rule.

``Lineage``
  For ``mutation.flip_tabu``, the realizability flips a lineage has seen per slot (``SlotKey``, ``SlotFlips``). ``DiversityState`` holds each bred offspring until the generation is scored, then records its flips and reports each parent's tabu slots to mutation.

.. doxygenfile:: diversity.hpp
