genetic/simplify.hpp
====================

``simplify_offspring`` is the last step of breeding a FRETISH offspring: it simplifies the condition, response and stop condition of each requirement that mutation may touch, and keeps the unsimplified offspring if simplification would merge two requirements. The TLSF counterpart is ``tlsf_simplify`` in ``tlsf/mutation.hpp``.

.. doxygenfile:: genetic/simplify.hpp
