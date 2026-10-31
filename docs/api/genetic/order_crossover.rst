genetic/order_crossover.hpp
===========================

The order-aware FRETISH crossover behind ``[genetic] p_order_crossover``. ``order_crossover_kind`` picks the operator from the two parents' realizability verdicts: a join of two realizable parents, a meet of two unrealizable ones, and path relinking for one of each. ``parent_verdict`` reads a realizable verdict from the status objective and an unrealizable one from ``RealizabilityChecker::proved_unrealizable``. The join and meet are built on ``specification_join`` and ``specification_meet`` from ``genetic/order_moves.hpp``, a slot they cannot bound taking one parent's value on a coin. ``OrderCrossoverStats`` counts the crossovers attempted, proved and scored realizable per kind.

.. doxygenfile:: genetic/order_crossover.hpp
