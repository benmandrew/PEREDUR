genetic/order_step.hpp
======================

The language-neutral vocabulary of the order moves: ``MoveOrder`` (how a child relates to its parent under implication), ``StepSide`` and ``StepKind``, and ``OrderStep``, one proved strengthening of a specification, templated over the specification type and the enumeration of its requirement fields. FRETISH's ``Step`` (``genetic/order_moves.hpp``) is ``OrderStep<Specification, RequirementField>``.

.. doxygenfile:: genetic/order_step.hpp
