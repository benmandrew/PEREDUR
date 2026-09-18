runner/process.hpp
==================

The fork/exec wrapper every other runner in this directory is built on. ``execute_and_capture`` runs a command, merges its stdout and stderr into the returned ``ProcessResult``, and reaps the child before returning. Each runner used to carry its own near-copy of this, which is how the timeout and orphan-containment policy came to differ between them.

**Timeouts.** A non-zero ``timeout`` is a wall-clock budget over the whole call, covering both waiting for output and reaping. Reaping is inside the budget deliberately: EOF on the pipe means the child closed stdout, not that it exited, so a tool that wedges after writing its answer would otherwise outlive its timeout inside ``wait4``. Zero means no deadline, and is the only way the call can block indefinitely.

**Containment.** Two mechanisms, because a timeout and a killed parent are different failures:

* The child is put in its own process group, and expiry kills the *group*, not the pid. A bare ``kill`` reaches only the direct child and strands every grandchild as an orphan reparented to PID 1. The parent repeats the child's ``setpgid`` so the group exists whichever side the scheduler runs first — otherwise a timeout firing in that window would signal a group that does not exist yet.
* ``PR_SET_PDEATHSIG`` asks the kernel to SIGKILL the child if this process dies first, covering a campaign harness enforcing a budget, the OOM killer, and Ctrl-C. The ``getppid() == 1`` check closes the race where the parent died before the request was registered.

The second mechanism is opt-in through ``ParentDeathPolicy``, because the kernel ties PDEATHSIG to the forking *thread* rather than to the process. That is exactly right for ``execute_and_capture``, which forks and waits in one place, and wrong for anything longer-lived: a child spawned lazily by the first pool worker to need it would be killed the moment that worker returned. The persistent formaliser therefore opts out and relies on its destructor.

What this does not cover: PDEATHSIG applies to the direct child only and is not inherited across that child's own ``fork``, so a grandchild still outlives a parent that dies without going through the timeout path. Nor does anything cover an abnormally killed process that owned a ``SurviveParentThread`` child. Placing the child in its own group also takes it out of the terminal's foreground group, so a Ctrl-C no longer reaches the tool directly — the PDEATHSIG half is what covers that case instead.

``spawn_piped_child`` and ``reap_with_grace`` are exposed for ``PersistentProcess`` (see :doc:`formaliser`), whose child outlives individual calls, and ``harden_child_after_fork`` for its tests.

.. doxygenfile:: process.hpp
