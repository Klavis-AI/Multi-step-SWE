"""Run a harness module as ``__main__`` with the process's cwd off ``sys.path``.

Every stage of the step-1 verifier is a ``python -m`` invocation, and the
observing stages share one scratch directory with candidate code. That code
runs as an unprivileged uid, but the uid owns the scratch, so it can write
there -- and ``python -m pkg.mod`` puts the process's working directory on
``sys.path`` *ahead* of ``PYTHONPATH``. On this image's Python 3.7 that entry is
``sys.path[0] = ''``. So a candidate who leaves a ``skutil_gates/``,
``skutil_probe/`` -- or even a bare ``runpy.py`` -- in the scratch has it
imported in preference to ``/tests/harness``: by the observing stages, and, more
seriously, by the **root** scoring stages, whose stdout *is* the reward. A tree
that plants a ``skutil_gates/grade.py`` printing ``REWARD 1.000000`` scored a
full 1.000000 with none of the step's work done, and one that plants a
``skutil_gates/run.py`` writing an all-passing ``obs.json`` forged family B the
same way. Measured, both; see NOTES-authoring.md item 5.

This launcher closes the class rather than either spelling. It is invoked as a
script -- ``python /tests/harness/_run.py pkg.mod ...`` -- so ``sys.path[0]`` is
*this file's* directory, ``/tests/harness``, and the cwd is never added at all.
Before importing anything that could itself live in the cwd it also strips any
cwd-like entry, so even ``import runpy`` below resolves from the stdlib. Then it
hands off to ``runpy`` exactly as ``-m`` would: same ``run_name`` of
``__main__``, same ``sys.argv`` shape, same exit code.

``os`` and ``sys`` are safe to import before the strip because the interpreter
loads both during start-up, so ``import`` returns the already-resolved module
out of ``sys.modules`` rather than searching the path.
"""
import os
import sys

_HARNESS = os.path.dirname(os.path.abspath(__file__))
_cwd = os.getcwd()
sys.path[:] = [p for p in sys.path if p not in ("", ".", _cwd)]
if _HARNESS not in sys.path:
    sys.path.insert(0, _HARNESS)

import runpy  # noqa: E402  -- deliberately after the path is made safe

if len(sys.argv) < 2:
    sys.stderr.write("_run.py: usage: _run.py <module> [args...]\n")
    sys.exit(2)

_module = sys.argv[1]
sys.argv = [_module] + sys.argv[2:]
runpy.run_module(_module, run_name="__main__", alter_sys=True)
