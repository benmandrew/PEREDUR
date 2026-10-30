fitness/semantic_similarity.hpp
================================

Computes semantic similarity between requirements and specifications using bounded model counting. The core metric is the harmonic mean of two directional log ratios:

.. math::

   \text{sim}(r, r') = \frac{2 \log \text{shared}(r, r', k)}
     {\log \text{count}(r, k) + \log \text{count}(r', k)}

where *shared(r, r', k)* is the number of traces of length *k* satisfying both requirements and *count(r, k)* counts traces satisfying *r* alone. A count of 1 falls back to membership, since its logarithm is 0. A score of 1.0 means identical trace semantics. Specification-level similarity averages pairwise requirement similarities. The default bound *k* is ``Config::default_model_counting_bound``.

.. doxygenfile:: semantic_similarity.hpp
