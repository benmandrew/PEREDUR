PEREDUR
=======

Repairing unrealisable reactive specifications, written either as FRETISH requirements or as TLSF.

The overview pages describe the algorithm, the tuning knobs, and the TLSF path. The pages below them are the curated public API — the ``include/`` headers.

.. toctree::
   :maxdepth: 1
   :caption: Overview

   architecture
   configuration
   tlsf

.. toctree::
   :maxdepth: 1
   :caption: Core

   api/bounded_async
   api/config
   api/config_io
   api/dashboard
   api/formula_key
   api/hash_combine
   api/profile
   api/prop_formula
   api/requirement
   api/serialisation
   api/status_line
   api/thread_pool
   api/version

.. toctree::
   :maxdepth: 1
   :caption: Crash

   api/crash/crash_handler

.. toctree::
   :maxdepth: 1
   :caption: Fitness

   api/fitness/function
   api/fitness/model_counter
   api/fitness/semantic_similarity
   api/fitness/status
   api/fitness/syntactic_similarity
   api/fitness/transfer_matrix

.. toctree::
   :maxdepth: 1
   :caption: Filter

   api/filter/bloat
   api/filter/correctness
   api/filter/implication
   api/filter/implication_check
   api/filter/vacuity
   api/filter/well_separation

.. toctree::
   :maxdepth: 1
   :caption: Genetic

   api/genetic/accumulator
   api/genetic/crossover
   api/genetic/generation
   api/genetic/monotone
   api/genetic/mutation
   api/genetic/nsga2
   api/genetic/operators
   api/genetic/pipeline
   api/genetic/random_source
   api/genetic/scored

.. toctree::
   :maxdepth: 1
   :caption: Runners

   api/runner/black
   api/runner/formaliser
   api/runner/ganak
   api/runner/ltlfilt
   api/runner/process
   api/runner/spot

.. toctree::
   :maxdepth: 1
   :caption: TLSF

   api/tlsf/specification
   api/tlsf/parser
   api/tlsf/writer
   api/tlsf/fitness
   api/tlsf/mucs
   api/tlsf/mutation
   api/tlsf/crossover
   api/tlsf/operators
   api/tlsf/filter
   api/tlsf/pipeline
