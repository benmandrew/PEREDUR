genetic/generation.hpp
=======================

The main driver of one generation of the genetic repair loop. Key types:

``FilterFunction``
  A named, stateful wrapper around a population-to-population callable that also tracks the input and output sizes of the most recent invocation for diagnostic output. Constructed implicitly from any compatible callable.

``ScoredSpecification``
  A ``Specification`` paired with its aggregated fitness score.

``evolve_generation`` runs truncation selection (sort by fitness, take top *N* as parents) with elitism: the best *E* parents carry over verbatim, bypassing crossover, mutation, and the filters, so the fittest candidates are never lost to a stochastic operator. The remaining parents produce offspring via crossover and mutation; the per-generation filter pipeline runs on those offspring, the elites are added back, the result is padded to *N* if filters removed too many, then scored. Progress callbacks are forwarded to the scoring step.

The factory function ``get_filter_functions`` assembles the standard in-generation filter sequence.

.. doxygenfile:: generation.hpp
