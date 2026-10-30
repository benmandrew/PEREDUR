runner/ltlfilt.hpp
==================

Wraps SPOT's ``ltlfilt`` for the three passes this codebase drives: ``--remove-wm`` (``rewrite_weak_operators``), ``--satisfiable`` (``spot_satisfiable``) and ``--equivalent-to`` (``ltl_equivalent``). ``rewrite_weak_operators`` memoises on the canonical key, so the subprocess runs at most once per formula rather than once per spelling of one. On binary inaccessibility, a non-zero exit or a timeout it returns nothing at all, its caller needing the rewrite to have actually happened. ``spot_satisfiable`` is deliberately unmemoised, its caller caching the decided verdict a layer up. ``LtlfiltStats`` counts each entry point's execs separately, since reporting one pass's count against the wall time of all of them understated the true figure by 35.5%.

A ``simplify_ltl`` wrapper over ``--simplify`` was removed once no engine path called it. ``SatisfiabilityChecker::check_satisfiability`` was its last caller, and dropped it because the pass cost 8x the ``--satisfiable`` call it preceded. The pass also blows up super-exponentially on the deep nested-X conjunctions the search builds, and it eliminates a variable wherever one term subsumes another, which ``run_ganak_on_formula`` cannot afford because its caller multiplies the count over the wider alphabet.

.. doxygenfile:: ltlfilt.hpp
