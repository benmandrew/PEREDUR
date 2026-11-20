tlsf/specification.hpp
======================

The ``tlsf::Specification`` type: a basic-format TLSF (Temporal Logic Synthesis Format) specification decomposed into its six named sections (``INITIALLY``, ``PRESET``, ``REQUIRE``, ``ASSUME``, ``ASSERT``, ``GUARANTEE``) plus the input/output signal universes and a ``Semantics`` tag (Mealy/Moore × standard/strict).

``to_ltl`` lowers a specification to a single LTL formula in SPOT syntax using the standard-implication semantics: the assumption side conjoins ``conj(INITIALLY)``, ``G(conj(REQUIRE))``, and ``conj(ASSUME)``; the guarantee side conjoins ``conj(PRESET)``, ``G(conj(ASSERT))``, and ``conj(GUARANTEE)``; the result is ``assumptions -> guarantees`` (or just the guarantee side when no assumption term is present). Strict semantics are recorded but not yet distinguished from standard.

.. doxygenfile:: tlsf/specification.hpp
