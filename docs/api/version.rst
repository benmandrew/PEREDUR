version.hpp
===========

Records which git commit a binary was built from, so a result can be traced back to the code that produced it. Every binary accepts ``--version`` and answers with three ``key=value`` lines::

   $ peredur --version
   commit=b929201d0c0e5a5f9b0a5a7d5f2f3b1c4d6e8a90
   commit_short=b929201
   dirty=0

The commit is resolved at *build* time, not configure time. A ``git rev-parse`` run from ``CMakeLists.txt`` records whatever HEAD was when cmake last ran, so ``cmake --build`` after a new commit keeps reporting the old hash — which is precisely the case the flag exists to catch. Instead ``cmake/version.cmake`` declares a target that runs ``cmake/write_version_header.cmake`` in script mode on every build; that script renders ``cmake/version.hpp.in`` through ``configure_file``, which rewrites the header only when the content changed. A build that changed no commit therefore costs one ``git rev-parse`` and invalidates nothing.

Only ``src/version.cpp`` includes the generated header, and only ``peredur_core`` has its directory on the include path. A new commit recompiles that one translation unit and relinks; the rest of the tree is untouched.

``dirty`` reports modified *tracked* files (``git status --porcelain --untracked-files=no``). Untracked files are excluded on purpose: they are not compiled into anything, so they cannot explain a binary that disagrees with its commit. A source tree that is not a git work tree at all — an exported tarball, say — falls back to ``unknown`` rather than failing the build.

All six binaries answer the flag — ``peredur``, ``realize``, ``compare``, ``ltl``, ``mucs`` and ``maximal`` — since a campaign's numbers come out of ``peredur`` and ``compare`` together, and knowing which built one of them is worth little without the other.

``scripts/run_experiments.py`` shells the flag once at startup, records ``commit`` and ``dirty`` on every CSV row and in the per-host launch manifest, and refuses to start when a binary's commit differs from the working tree's HEAD or the binary was built dirty. ``--allow-stale-binary`` downgrades that to a warning.

.. doxygenfile:: version.hpp
