psp/fitness.hpp
===============

The PSP objectives. Syntactic similarity is the keyword token similarity with the formula similarity of P, S, Q and R. Semantic similarity counts the models of each changed slot's lowering, averaged with the keyword similarity. Status is the MRS walk over the live guarantees, each subset checked by ``ltlsynt`` over ``to_ltl()``. The factory in ``src/fitness/factory.hpp`` builds them into the fitness function.

.. doxygenfile:: psp/fitness.hpp
