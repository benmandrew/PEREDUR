fitness/transfer_matrix.hpp
============================

The ``TransferSystem`` type represents a finite-state automaton for bounded trace model counting. Construction (via ``build_transfer_system_from_ltl``) calls ``ltl2tgba`` to generate a deterministic HOA automaton from an LTL formula, then calls Ganak to compute per-transition valuation counts, producing a ``CountMatrix`` whose (i, j) entry is the number of satisfying valuations on the transition from state *i* to state *j*.

``count_joint_atoms`` gives the shared atom universe of a pair of requirements, so that the system built over their conjunction extends the mechanism to pairs, enabling the numerator *shared(r, r', k)* in the semantic similarity formula to be computed.

``Count`` is either ``uint64_t`` or ``unsigned __int128`` (configured at build time). All arithmetic on ``Count`` values must use ``count_add_overflow`` / ``count_mul_overflow`` with an assertion on the overflow flag.

.. doxygenfile:: transfer_matrix.hpp
