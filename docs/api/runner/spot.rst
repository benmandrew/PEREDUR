runner/spot.hpp
================

Wrappers for two SPOT tools:

``ltl2tgba`` (via ``run_ltl2tgba_for_counting``)
  Converts an LTL formula to a deterministic, state-based HOA automaton used by ``build_transfer_system``. Results are memoised by formula string via ``Ltl2tgbaStats`` counters.

``ltlsynt`` (via ``RealizabilityChecker``)
  Checks whether a specification is realisable by running ``ltlsynt`` on the conjunction of its assumption and guarantee LTL formulae. Results are memoised by full formula string. ``global_real_checker()`` returns the process-lifetime instance shared by all callers.

Both binary paths sit under the directory ``spot_bin_dir()`` returns: the ``PEREDUR_SPOT_BIN_DIR`` environment variable where it is set and non-empty, and otherwise the ``SPOT_BIN_DIR`` preprocessor definition baked in at build time (set by ``cmake/spot.cmake``). The environment is read once, on first use, so a binary can be relocated away from the tree it was built in.

.. doxygenfile:: spot.hpp
