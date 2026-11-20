psp/filter.hpp
==============

The whole-specification implication check (``psp::spec_implies``) and the valid-guarantee test (``psp::has_valid_guarantee``) for ``psp::Specification``, which ``SpecFormat<psp::Specification>`` hands to the offline drivers. The implication check is exact. Where the rule in ``psp/implication.hpp`` covers every live assumption, the query leaves out the guarantees it covers, as on the FRETISH path.

.. doxygenfile:: psp/filter.hpp
