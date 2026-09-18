requirement.hpp
===============

Defines the core domain types used throughout the tool:

``Timing``
  A ``std::variant`` over nine timing modalities (``Immediately``, ``NextTimepoint``, ``WithinTicks``, ``ForTicks``, ``AfterTicks``, ``Eventually``, ``Always``, ``Until``, ``Before``) that encode FRETISH temporal operators as algebraic types. ``Until`` and ``Before`` carry a propositional stop condition.

``ConditionType``
  Controls whether a requirement's condition is evaluated as a rising-edge trigger (``Trigger``) or at every timepoint it holds (``Continual``).

``Requirement``
  The unit of repair. Holds a condition ``Formula``, a response ``Formula``, a ``Timing``, a ``ConditionType``, and the derived LTL string produced by ``requirement_to_ltl`` — a ``std::optional<string>`` that is ``nullopt`` when the formula could not be expressed in SPOT syntax.

``Specification``
  A list of assumption ``Requirement`` objects, a list of guarantee ``Requirement`` objects, and the input/output atom universes used during crossover and mutation.

``State``
  An automaton state for the transfer-matrix model counter, encoding the condition/response/countdown flags used when building ``TransferSystem`` objects.

.. doxygenfile:: requirement.hpp
