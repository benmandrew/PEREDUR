psp/requirement.hpp
===================

The requirement of the PSP front end (Dwyer's Property Specification Patterns):

``Pattern``
  A ``std::variant`` over the nine patterns of PLAN.md D3, as ``timing::Timing`` is for FRETISH: ``Absence``, ``Universality``, ``Existence``, ``Precedence``, ``Response``, and the bounded ``BoundedResponse``, ``BoundedInvariance``, ``BoundedExistence`` and ``BoundedRecurrence``, each with its count.

``PspScopeKind``
  The five scopes: ``Globally``, ``Before`` (R), ``After`` (Q, opened at the first Q), ``Between`` (Q and R) and ``AfterUntil`` (Q and R). Each is closed on the left and open on the right.

``psp::Requirement``
  A pattern under a scope over the propositional fields P and S (the pattern's) and Q and R (the scope's), each present exactly where it is read, or a fixed raw-LTL entry that is never weakenable. It carries its lowering in ``m_ltl``, a ``m_weakenable`` lock and an ``m_removed`` tombstone. ``to_string`` prints Konrad and Cheng's structured English.

``requirement_to_ltl``
  The body-and-wrapper lowering of PLAN.md D4, implemented in ``src/psp/lowering.cpp`` and tabled in ``docs/dev/psp.md``. It is a port of the lowering in ``scripts/psp_field_orders.py``, and the ``psp_lowering`` suite checks the two equivalent for every pattern and scope at counts 1 to 4, and the lowering against the Dwyer web table.

.. doxygenfile:: psp/requirement.hpp
