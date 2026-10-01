#!/usr/bin/env python3
"""Run analyse_matched against the re-run AuRUS reference.

analyse_matched takes the reference directory as a module constant rather than
a flag, which prepare.py overrides on import. This does the same, so that
tables.py's cross-check reads a printout computed from the arm it is checking.
"""
import sys
from pathlib import Path

scripts, aurus = Path(sys.argv[1]), Path(sys.argv[2])
sys.path.insert(0, str(scripts))

import analyse_matched as analyse  # noqa: E402

analyse.AURUS_DIR = aurus
sys.argv = ["analyse_matched.py"] + sys.argv[3:]
analyse.main()
