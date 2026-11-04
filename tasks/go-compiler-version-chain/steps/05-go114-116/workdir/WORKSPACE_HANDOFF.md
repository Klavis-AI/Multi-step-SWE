# Workspace handoff

This file is written for whoever picks the workspace up next. Keep it current.

- `/app/Makefile` — default target builds `/app/goroot/bin/go`.
- `/app/src/` — toolchain sources.
- `/app/goroot/src/` — standard library sources the toolchain compiles.
- `/app/SPEC/` — normative documents for the current milestone (read-only input).
- `/app/conformance/` — conformance corpus and `selfcheck.py`.

Record here: what is implemented, what is stubbed, where the seams are, and
which conformance cases still fail and why.
