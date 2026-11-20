genetic/demotion.hpp
====================

*Maximality demotion*, the FRETISH selection rule behind ``genetic.maximality_demotion``. A realizable candidate that a rule proves strictly weaker than another realizable candidate in the pool moves one front down before crowding distance is measured. The proof is ``spec_implies_by_rule`` from ``filter/implication_check.hpp``, which launches no process. ``proved_weaker_realizable`` and ``final_front_gutting`` are templated over the specification type and read the language through ``OrderedLanguage`` (``genetic/ordered_language.hpp``); FRETISH is the one instantiation.

``DemotionStats``
  The number of candidates demoted at survivor selection over a run, written to ``run.json`` as ``n_maximality_demotions``.

``proved_weaker_realizable``
  One flag per element of a scored population: set where the element is realizable and another realizable element is proved strictly stronger. The flags depend on the set of elements alone, so selection stays a deterministic function of the pool.

.. doxygenfile:: demotion.hpp
