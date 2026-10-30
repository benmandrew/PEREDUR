tlsf/pipeline.hpp
=================

``tlsf::run_repair`` is the end-to-end TLSF genetic-repair driver. It parses a basic-TLSF input, evolves a population under the TLSF fitness, filters, and genetic operators (:doc:`fitness`, :doc:`filter`, :doc:`crossover`, :doc:`mutation`), collects the realizable, deduplicated survivors, and writes each to ``<output_dir>/repair_N.tlsf`` with a ``repair_N.fitness.json`` sidecar. It returns the filter report's rows and leaves the closing reports and the manifest to its caller, ``run_tlsf_repair`` in ``src/repair/driver.cpp``, which ends the FRETISH run the same way.

.. doxygenfile:: tlsf/pipeline.hpp
