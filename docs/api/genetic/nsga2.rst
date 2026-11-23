genetic/nsga2.hpp
=================

The NSGA-II selection primitives for multi-objective evolution, an alternative to the weighted-average scalar used by the default scheme. They rank a population by Pareto dominance and diversity rather than a single blended score, so the search can spread across the Pareto front of realizable repairs.

``dominates``
  Pareto dominance under the project convention that higher is better: one objective vector dominates another when it is at least as good in every objective and strictly better in at least one.

``non_domination_ranks``
  Deb's fast non-dominated sort. Returns the 0-based front index of each individual (0 = the Pareto front, higher = increasingly dominated).

``crowding_distances``
  The crowding distance of each individual within its own front (Deb et al. 2002): the range-normalised gap between an individual's neighbours summed over the objectives, with the per-front extremes of every objective given an infinite distance so the boundaries of the front are always preferred.

``farthest_point_scores``
  A crowding score per individual from greedy farthest-point selection under a pairwise distance, front by front, each front starting from the members of every better front. Sorting a front by the score descending gives the pick order. ``genetic.crowding = "specification"`` uses it in place of ``crowding_distances``.

``Nsga2Options``
  What ``nsga2_sort`` may change: an extra objective per element, appended for ranking (``genetic.novelty``), and a distance that replaces objective crowding with ``farthest_point_scores``. The default changes nothing.

``nsga2_sort``
  Fills each ``Scored<Spec>`` element's ``rank`` and ``crowding_distance`` from its objective vector, then stable-sorts the population by the crowded comparison operator (front rank ascending, then crowding distance descending).

.. doxygenfile:: nsga2.hpp
