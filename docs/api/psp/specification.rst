psp/specification.hpp
=====================

``psp::Specification`` holds assumptions, guarantees and the input and output atom lists, the top-level shape of the FRETISH specification. ``to_ltl`` lowers it to ``(assumptions) -> (guarantees)`` over the live requirements.

The ``.psp`` format is JSON, described by ``schemas/psp-specification.schema.json``. ``parse_specification`` and ``load_specification`` read it, reject an unsafe or undeclared atom name, and prefix every atom with ``k_atom_prefix`` as the FRETISH loader does. ``write_specification`` writes it back with the prefix stripped. ``SpecFormat<psp::Specification>`` in ``src/spec_format.hpp`` routes the ``ltl``, ``compare``, ``maximal`` and ``lint-ideals`` drivers to it for a ``.psp`` input.

.. doxygenfile:: psp/specification.hpp
