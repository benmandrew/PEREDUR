genetic/ordered_language.hpp
============================

The traits through which the order-directed mechanisms read a specification language. ``OrderedLanguage<Spec>`` is specialised once per language, and each specialisation provides slot access, the implication order by rule (``requirement_implies``, ``spec_implies_by_rule``, ``classify_move``, ``requirement_join``, ``requirement_meet``, ``keyword_similarity``), the strengthening steps (``steps_towards``, ``step_chain``, ``with_slot_value``), a realisability check, and the per-slot directed mutator (``mutate_slot``, ``free_rewrite_rates``). The output climb (``genetic/climb.hpp``), two-way mutation in exchange mode and the blame narrowing (``genetic/mutation.hpp``), maximality removal (``genetic/demotion.hpp``) and novelty (``genetic/diversity.hpp``) are templated over ``Spec`` and call only these. FRETISH is the one instance, and it forwards each trait to the function the mechanisms called before they were templated.

.. doxygenfile:: genetic/ordered_language.hpp
