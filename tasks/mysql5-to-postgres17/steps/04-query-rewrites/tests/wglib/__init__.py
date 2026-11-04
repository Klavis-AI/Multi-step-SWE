"""
The scorer's instrument set, shared verbatim by every step's verifier.

`test.sh` runs `python3 -m wglib.score /tests/plan.py`; the plan names this
step's families and their weights, and score.py runs them behind gate.py's
rule that an unevaluable check is a failure rather than a skip.

This file exists so `wglib` is a regular package rather than a namespace one.
The modules import each other with `from . import x`, which a namespace package
would also support, but only for as long as nothing else named `wglib` ever
appears ahead of /tests on the path. Six bytes of insurance.
"""
