dashboard.hpp
=============

Streams a run's progress to ``<output-dir>/progress.jsonl`` as it happens, and copies the dashboard page beside it, so serving the output directory is all a viewer needs::

   peredur --dashboard --input <spec> --output-dir <dir>
   python3 -m http.server -d <dir> 8000

Opt-in: ``peredur --dashboard`` or ``[runtime] dashboard = true``. A writer constructed disabled touches nothing and every method on it is a no-op, so neither driver branches on whether progress was asked for.

The page polls once a second. ``?poll=<seconds>`` on the URL changes that — ``?poll=5`` for a slow run, ``?poll=0.25`` for a fast one, ``?poll=0`` to load once and never re-poll when reading a finished or archived run. Values are clamped to 0.25--300 s, and an unparseable one falls back to the default: each poll refetches and reparses the whole log, which grows with the run, so a very fast interval costs more the longer the run goes on. The status pill reports the rate in force.

``DashboardWriter`` appends one JSON object per line and flushes each as it is written — the page polls the file mid-run, so a buffered line is a line the viewer cannot see. Four record types are emitted, each tagged with ``type``: ``run_start`` (input, generation and population counts, seed, objective names), ``stage`` (one per pipeline stage per generation, with its name, ``n_in``, ``n_out`` and elapsed time), ``generation`` (timing, best and mean fitness, per-objective means, realizable count) and ``run_end``.

No stage name is written down anywhere. ``run_start`` carries the roster of every stage a generation *can* run, built by ``generation_stage_names`` from the same pipeline the run uses, so it cannot drift from it; the ``stage`` records then say which of them actually ran. A new filter or pipeline stage therefore appears without either side being taught about it.

The roster exists so the page can lay the stage chart out at a fixed height with each stage on a fixed row. A generation need not report every stage in the roster — MUC repair evolves a fresh core per iteration, and a filter the config disables is never constructed — and a chart sized to whichever stages happened to run would change height underneath the reader. Stages absent from a generation keep their row, struck through and greyed.

Optional fields are omitted rather than defaulted. ``n_realizable`` is absent when the driver does not measure it per generation — the TLSF path checks realizability only at the end of a run — so the page can distinguish "not measured" from "none found". ``muc_iter`` appears only under MUC-guided repair, which restarts its generation count per core.

A writer that cannot open its file reports once and then does nothing: losing the progress log must never take a repair run with it.

.. doxygenfile:: dashboard.hpp
