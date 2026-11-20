psp/seeding.hpp
===============

The faults of the PSP seeded corpus. ``seeded_edits`` lists every single-slot edit of a realisable specification that the implication rule does not prove weaker: a step up the pattern, scope or count order, a move to an incomparable pattern or scope, a narrowed formula field, a flipped literal or a swapped signal. ``sample-mutants --psp-seeded`` writes them, and ``experiments/2026-11-20-psp-seeded-corpus/scripts/seed.py`` keeps the unrealisable ones.

.. doxygenfile:: psp/seeding.hpp
