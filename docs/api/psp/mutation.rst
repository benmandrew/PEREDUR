psp/mutation.hpp
================

The PSP operators. ``mutate_requirement`` moves the pattern, the scope and the count along their orders and rewrites P, S, Q and R by polarity, each arm behind its ``[psp]`` probability. ``mutate_specification`` is an instance of the template that FRETISH's whole-specification mutation also instantiates, so the structural actions, two-way mutation and blame narrowing are shared. ``crossover_specifications`` crosses one field of one slot pair.

.. doxygenfile:: psp/mutation.hpp
