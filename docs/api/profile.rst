profile.hpp
===========

An opt-in named-scope profiler. ``PEREDUR_PROFILE_SCOPE("name")`` times the enclosing block, recording call count, total wall time, total per-thread CPU time and the slowest single call against that name.

Recording wall and CPU separately is what the existing per-tool timers cannot do: a scope with near-zero CPU but large wall time is blocked on something (a child process, a lock) rather than computing, and the ratio says which.

Inert unless the ``PEREDUR_PROFILE`` environment variable is set. Set it to a file path to write the report as JSON as well as to stderr, or to ``1`` or ``-`` for the stderr table alone. Disabled, a scope costs one relaxed atomic load.

Accumulators are atomic and sites are function-local statics, so the scoring thread pool does not serialise on the profiler while it is being measured.

.. doxygenfile:: profile.hpp
