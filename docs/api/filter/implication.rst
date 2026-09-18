filter/implication.hpp
======================

Population filters based on the logical implication partial order between specifications, each a template instantiated for the FRETISH ``Specification`` (the default) and ``tlsf::Specification``:

``make_dedup_filter``
  Drops structurally identical duplicate specifications, keeping the first occurrence in input order.

``make_implication_filter``
  Final-pass filter. Keeps only the maximal elements of the population under the implication order — specs that are not strictly dominated by any other survivor. All *n(n-1)/2* pairwise checks run in parallel via the thread-safe ``SatisfiabilityChecker``.

``syntactic_similarity_key`` builds the ranking that picks the survivor of an equivalence class, and ``SimilarityKeyT`` is its type.

``ImplicationFilterStats`` exposes atomic counters for comparisons, skips, duplicates, and timeouts from the most recent implication-filter sweep.

.. doxygenfile:: implication.hpp
