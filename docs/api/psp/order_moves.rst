psp/order_moves.hpp
===================

The order moves on PSP requirements that the order-directed mechanisms read through ``OrderedLanguage<psp::Specification>``. ``classify_move`` relates a child to its parent by rule. ``requirement_join`` and ``requirement_meet`` bound two requirements field by field, by polarity. ``steps_towards`` lists the proved one-field steps back towards the original, with the restore of a tombstoned guarantee and the drop of an appended assumption, as on FRETISH. ``count_chain``, ``count_step_chain`` and ``with_slot_count`` give the count chain that a bisected pattern step crosses. ``keyword_similarity`` compares the downsets of two patterns and two scopes. ``empties_scope`` and ``gutted_guarantees`` find the repairs that the output flags as gutted.

.. doxygenfile:: psp/order_moves.hpp
