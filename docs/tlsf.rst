TLSF specifications
===================

Alongside FRETISH, PEREDUR repairs basic *Temporal Logic Synthesis Format* (TLSF) specifications directly — the interchange format used by the reactive-synthesis community. A ``.tlsf`` input is auto-detected from the file extension, or forced with ``--format tlsf``.

In TLSF mode the same genetic machinery evolves the six specification sections — ``INITIALLY``, ``PRESET``, ``REQUIRE``, ``ASSUME``, ``ASSERT`` and ``GUARANTEE`` — rather than FRETISH requirements. The fitness function mirrors the FRETISH one (semantic similarity, realisability status, and syntactic similarity) but scores whole TLSF formulae. See :doc:`configuration` for the weights.

Repairs are written back as valid TLSF, so a repair can be fed straight into ``realize``, ``ltl``, or an external synthesiser. Each ``repair_N.tlsf`` is paired with a ``repair_N.fitness.json`` holding its score.

A worked example
----------------

The bundled ``examples/lily02`` is a grant arbiter, taken from the Lily demo set. It reads three inputs — ``req``, ``cancel`` and ``go`` — and drives one output, ``grant``, under three guarantees:

.. code-block:: text

   INPUTS  { req; cancel; go; }
   OUTPUTS { grant; }
   GUARANTEES {
     G(req -> X (grant || X (grant || X grant)));   // answer within three ticks
     G(grant -> X !grant);                          // never twice in a row
     G(cancel -> X (!grant U go));                  // after cancel, wait for go
   }

This is unrealisable. The third guarantee uses a *strong* until, so a ``cancel`` obliges the system to withhold grants until a ``go`` that the environment is never required to send. Meanwhile the first guarantee still demands that any ``req`` be answered within three ticks. Hold ``go`` low forever and issue a request, and the two obligations cannot both be met:

.. code-block:: sh

   realize examples/lily02/spec.tlsf   # UNREALIZABLE

The intended fix constrains the environment instead of the system. ``examples/lily02/fixes/add_assumption.tlsf`` adds the assumption ``G(cancel -> X go)`` — a cancel is always followed by a go — and leaves all three guarantees untouched.

What the search finds
---------------------

.. code-block:: sh

   peredur --input examples/lily02/spec.tlsf --output-dir out --seed 42 --repair-summary

That run reports ``Realizable specifications: 95 (26 maximal, 11 equivalent)`` and writes the 26 maximal repairs, in about 3 seconds on 20 threads. The seed fixes which repairs come out, not how long they take — that swings with how the external solvers get scheduled.

After the count, ``--repair-summary`` lists what each repair changed. A ``-`` line is a formula of the input that the repair dropped, and a ``+`` line is a formula it added, both in the writer's fully parenthesised form. The second and third blocks read:

.. code-block:: text

   repair_1.tlsf
     GUARANTEE - G((cancel) -> (X((!(grant)) U (go))))
     GUARANTEE + G(F(((cancel) && (!(grant))) U (grant)))

   repair_2.tlsf
     GUARANTEE - G((cancel) -> (X((!(grant)) U (go))))
     GUARANTEE + G((req) -> (X(((req) || (cancel)) U (!(grant)))))

Of the 26 repairs, 25 drop or rewrite the third guarantee. Six of those change nothing else, 14 also rewrite the first guarantee, and 4 also rewrite the second. Four repairs add an assumption, and none of the four is the intended fix. Every repair passes ``realize``, which is the definition of a repair that the search is given. Reading down the list and checking what each repair gave up is the intended way to use it.

The order the files are written in is the algorithm's own ranking, not descending fitness. Under either NSGA-II scheme that is Pareto rank followed by crowding distance, which is why the weighted scalars of the first three repairs run 0.959, 0.969, 0.967. Only the ``weighted`` scheme sorts by the scalar. See :doc:`configuration`.

The assumption-adding move that would have found the intended fix is controlled by ``p_add_assumption``, shared with FRETISH mode; ``[tlsf.mutation]`` tunes the TLSF-specific operators. Both are described in :doc:`configuration`.

Minimal unrealisable cores
--------------------------

Only some of a specification's obligations are responsible for its unrealisability. A *minimal unrealisable core* (MUC) is the smallest subset of the guarantee side — the ``PRESET``, ``ASSERT`` and ``GUARANTEE`` sections — that is still unrealisable against the full, unchanged environment side. It names the culprit formulae, and dropping any one member of it restores realisability.

The ``mucs`` tool prints that core, and nothing else:

.. code-block:: console

   $ mucs examples/lily02/spec.tlsf
   core: 1 of 3 guarantee-side formulae
   [GUARANTEE] G((cancel) -> (X((!(grant)) U (go))))

The core is the third guarantee alone — the formula that 25 of the 26 repairs rewrite, arrived at by a different route. Where the search discovers it by trying edits and keeping what works, the extractor proves it by elimination, in a handful of synthesis calls rather than a population.

The environment side is held fixed in every query. Relaxing an assumption can only make synthesis harder, so it can never be part of a core. That leaves unrealisability monotone in the guarantee-side set: adding a system obligation can never restore realisability. Monotonicity is what makes a minimal unrealisable subset well defined, and what lets the extractor use *QuickXplain* (Junker 2004), at ``O(k log(n/k))`` synthesis calls for a core of size ``k`` out of ``n`` formulae. On an already-realisable input ``mucs`` prints ``REALIZABLE (no core)``. It is TLSF-only; FRETISH input is not supported.

MUC-guided repair
~~~~~~~~~~~~~~~~~

The same extraction drives an alternative repair strategy, selected with ``[tlsf] repair_mode = "muc"``:

.. code-block:: toml

   [tlsf]
   repair_mode = "muc"
   muc_max_iterations = 32

Rather than evolving the whole specification at once, it extracts a core and evolves only that sub-specification. It then reintegrates the repaired core with the untouched non-core guarantees, and repeats on the recombined specification until the whole thing is realisable or ``muc_max_iterations`` trips. The search space each round is the core rather than the entire guarantee side, and the formulae that were never at fault are carried through unedited instead of being exposed to mutation.

It returns a single repair rather than a ranked set, so it answers a different question from ``monolithic``: not "what are the plausible repairs, best first" but "give me one repair, having edited as little as possible". Expect its repair to strengthen, too. Nothing constrains a mutation to weaken, and evolving a core in isolation gives the search a narrow target it can hit by strengthening, reaching realisability while forbidding behaviour the original allowed. No screen catches that since the final weakening screen was removed on 2026-09-11. ``scripts/gen_configs.py --repair both`` crosses the two modes as an experiment factor.

Between them the two modes trade breadth against locality. The monolithic search says how many ways out of an unrealisable specification there are; the core says which formulae the search should have been looking at all along.
