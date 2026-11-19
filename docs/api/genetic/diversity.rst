genetic/diversity.hpp
=====================

The FRETISH mechanisms that spread the search over unlike repairs, each behind a key that is off by default (see "Diversity mechanisms" in ``docs/dev/algorithm.md``), and the counters each writes to ``run.json``.

``specification_distance``
  The share of assumption and guarantee slots in which two specifications hold different requirements, pairing slots by index. ``genetic.crowding = "specification"`` orders each NSGA-II front by farthest-point selection under it (``farthest_point_scores`` in ``nsga2.hpp``).

.. doxygenfile:: diversity.hpp
