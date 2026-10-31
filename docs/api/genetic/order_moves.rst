genetic/order_moves.hpp
=======================

The implication order over FRETISH specifications, decided by rule with no solver and no ``RandomSource``. ``classify_move`` reads a child as Equal, Stronger, Weaker or Incomparable against its parent at the level of the whole ``A -> G`` formula, flipping the assumption side. ``requirement_join`` and ``requirement_meet`` combine two requirements of one slot field by field and return a result only where ``requirement_implies`` proves it against both inputs. ``specification_join`` and ``specification_meet`` apply them slot by slot and leave each unproved slot empty, so a caller can fill it from a parent. ``steps_towards`` lists the proved one-field strengthenings that move a candidate back towards its original, and ``timing_chain`` gives the ``within`` and ``for`` timings between two counts for a climb to bisect. Nothing calls the library yet.

.. doxygenfile:: genetic/order_moves.hpp
