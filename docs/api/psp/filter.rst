psp/filter.hpp
==============

The whole-specification implication check (``psp::spec_implies``) and the valid-guarantee test (``psp::has_valid_guarantee``) for ``psp::Specification``, which ``SpecFormat<psp::Specification>`` hands to the offline drivers. The implication check is the exact one-query check the TLSF path uses. Phase P3 of the PSP plan adds the rule-based check beside it.

.. doxygenfile:: psp/filter.hpp
