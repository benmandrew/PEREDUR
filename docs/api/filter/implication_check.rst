filter/implication_check.hpp
============================

Provides ``spec_implies(from, dest, checker)``, which tests whether specification *from* logically implies *dest* as one query over the two whole-specification lowerings: ``from.to_ltl() & !dest.to_ltl()`` is unsatisfiable exactly when the implication holds. The check is exact, so a refusal is a fact about the two specifications rather than a limit of the check, and a timeout is reported as ``nullopt`` rather than as a verdict. It decomposed per requirement until 2026-09-11, matching each requirement against a single counterpart, which missed every implication holding only via several requirements together. Called by both the weakening and implication filters.

``spec_implies_by_rule`` proves the same implication without a solver, slot by slot: every live assumption of *from* and every live guarantee of *dest* must be covered by ``rule_cover``, through a matching lowering or ``requirement_implies`` on the entry in the same slot. It is sound and incomplete, and maximality demotion in selection reads it (``genetic/demotion.hpp``).

.. doxygenfile:: implication_check.hpp
