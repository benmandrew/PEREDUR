tlsf/fitness.hpp
================

TLSF analogues of the FRETISH fitness components — syntactic similarity, semantic (bounded-model-counting) similarity, and a graded realizability status — plus ``tlsf_get_fitness_function``, which assembles them into a weighted aggregate over ``tlsf::Specification`` gated on the same ``cfg.fitness_weight_*`` fields the FRETISH factory uses.

.. doxygenfile:: tlsf/fitness.hpp
