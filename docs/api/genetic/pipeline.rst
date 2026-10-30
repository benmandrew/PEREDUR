genetic/pipeline.hpp
====================

One generation of the repair loop expressed as an ordered list of named, instrumented stages rather than a fixed sequence of statements.

``GenerationContext<Spec>`` is the mutable state a generation is threaded through. It carries both the scored and the unscored view of the population, because the stages are not type-uniform: breeding turns scored parents into unscored offspring and scoring turns them back. ``population_size()`` reports whichever view is live, so a stage can be measured without knowing which side of that boundary it sits on.

``PipelineStage<Spec>`` mirrors ``FilterFunctionT``'s interface -- ``name()``, ``n_in()``, ``n_out()`` -- so a consumer can walk the list returned by ``make_generation_pipeline`` and report every stage without hardcoding which stages exist. ``run_generation_pipeline`` runs them in order and reports each completed stage, with its population sizes, to a ``StageObserver``.

Breeding is deliberately a single stage. Crossover and mutation are interleaved per offspring slot, so splitting them into separate passes over the whole population would reorder every random draw after the first slot and break seed reproducibility; the ``determinism`` test suite pins the resulting draw stream.

.. doxygenfile:: genetic/pipeline.hpp
