psp/implication.hpp
===================

The implication order over PSP requirements and specifications, decided by rule with no solver. ``pattern_implies`` and ``scope_implies`` transcribe the pattern order per scope class and the scope order per pattern cell that ``scripts/psp_field_orders.py`` measured into ``docs/dev/psp-orders.json``, and ``field_polarity`` the polarity of P, S, Q and R per cell. ``requirement_implies`` chains one-field steps over the six fields in every order, and ``spec_implies_by_rule`` and ``residual_destination`` lift it slot by slot to specifications, as ``filter/implication_check.hpp`` does for FRETISH. The ``psp_implication`` suite re-derives each table with ``ltlfilt``, and the opt-in ``psp_requirement_implies_sweep`` checks the rule against SPOT.

.. doxygenfile:: psp/implication.hpp
