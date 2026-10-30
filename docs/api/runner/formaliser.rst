runner/formaliser.hpp
======================

Wrapper around the external FRET requirement-formaliser CLI (``fretCLI.main.js formalize --logic ft-inf --batch``), a persistent Node.js process started once and reused rather than spawned per call like the other ``runner/`` tools. ``PersistentProcess`` owns the child's stdin/stdout pipes; ``RequirementFormaliser`` memoises results by requirement text and serialises subprocess round trips through ``m_proc_mutex``, since the child's single ordered stdin/stdout channel has no way to match a response back to its request if two callers' writes interleave.

``global_formaliser()`` returns the process-lifetime instance shared by all callers, so the memoisation cache and the single child process are shared across the entire run.

The formaliser is a test oracle. It is built into the ``peredur_formaliser`` library, which only the test binary and the fuzzer link: they check ``requirement_to_ltl`` against it, and no shipped binary calls it.

.. doxygenfile:: formaliser.hpp
