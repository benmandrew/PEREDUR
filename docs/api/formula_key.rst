formula_key.hpp
===============

Canonical cache keys for LTL formula strings. Every memoisation cache between the search and the external tools is keyed on a rendered formula, and ``Formula`` is a binary tree with no canonical shape, so one formula reaches a cache under many spellings and each spelling buys its own subprocess.

- **canonical** — the rendering of the formula's canonical form (see ``Formula::canonical``), preserving atom names. Taken by caches whose value names atoms: the ``ltl2tgba`` automaton store and ``rewrite_weak_operators``.
- **renamed** — the same, with atoms renamed to a canonical sequence in order of first appearance. Taken by caches whose value is invariant under a bijection on the atoms: the satisfiability cache and the trace-count cache.
- **realizability** — a partition-preserving renaming, plus the count of declared signals the formula never mentions. Taken by the ``ltlsynt`` cache, realizability being invariant under a bijection only when it preserves the input/output split.

Which key a cache may take is decided by what it stores. Reading a value that names atoms back through a renaming would mean renaming inside a tool's own output, and SPOT prints a unary operator hard against its operand (``Ffk1``), so no tokenisation of that output separates the operator from the name.

Each entry point memoises on its raw input, so a spelling already seen costs one hash rather than a parse, and hands out a reference into an append-only store.

.. doxygenfile:: formula_key.hpp
