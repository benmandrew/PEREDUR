genetic/diversity.hpp
=====================

The FRETISH mechanisms that spread the search over unlike repairs, each behind a key that is off by default (see "Diversity mechanisms" in ``docs/dev/algorithm.md``), and the counters each writes to ``run.json``.

``specification_distance``
  The share of assumption and guarantee slots in which two specifications hold different requirements, pairing slots by index. ``genetic.crowding = "specification"`` orders each NSGA-II front by farthest-point selection under it (``farthest_point_scores`` in ``nsga2.hpp``).

``DiversityState``
  The run-scoped state the diversity keys keep across generations, lent to each generation by the generation loop: the accumulator M, and for ``genetic.novelty`` a per-specification count of the members of M that rule-imply it, extended as M grows.

``novelty_objective``
  The extra objective ``genetic.novelty`` ranks by: minus that count for a realizable candidate, 0 for an unrealizable one.

.. doxygenfile:: diversity.hpp
