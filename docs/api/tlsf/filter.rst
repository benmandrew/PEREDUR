tlsf/filter.hpp
===============

The TLSF-specific filters for ``tlsf::Specification``: a vacuity guard dropping specifications that carry a trivial section literal (``false`` in INITIALLY/REQUIRE/ASSUME, ``true`` in PRESET/ASSERT/GUARANTEE), or a *valid* guarantee-section formula, or an unsatisfiable assumption-side conjunction — the TLSF counterpart of the FRETISH vacuity filter, run under the same stage name and, like it, in every generation — and ``tlsf_spec_implies``, the whole-specification implication check. Deduplication, the bloat cap, well-separation and the implication filter are templates shared with the FRETISH path, in the ``filter/`` headers.

.. doxygenfile:: tlsf/filter.hpp
