#!/usr/bin/env python3
"""Split one full recording of the pinned tree into the scored expectation and
the defect ledger.

    python3 lib/freeze.py <recording.json> <expected.json> <defects.json>

<recording.json> is `replay.py observe` run over `data/corpus.json` against the
pinned tree (`data/pristine.tar.gz` unpacked, on PYTHONPATH) in this step's
verifier image, as uid 65534, in the environment `harness.sh` `_runner_env`
pins -- exactly how the scorer replays a candidate, with GRADE_PYTHONPATH
pointing at the pinned tree instead of a venv. The unpacked tree has to be
owned by that uid, as the scorer's venv is writable by it: bento rewrites
`bento/parsetab` on its first parse, and against a tree it cannot write the
warm-up fails and the observations differ from the shipped ones wholesale
(5241 of 5646 scored values change; owned by uid 65534 it reproduces both
files byte for byte). Recorded twice; the two runs must be byte-identical
before either is frozen.

The rule is a partition by marker text, nothing else: an observation whose body
contains one of the two MARKERS below anywhere goes to the ledger, with that
marker and its reason; every other observation is scored, unchanged. The two
markers are the pinned tree's PEP 479 crash (`raise StopIteration` inside a
generator, a RuntimeError from Python 3.7 on) and bentomaker's top-level
handler reporting a crash through the command line. A candidate that fixes or
stops reaching those sites is neither rewarded nor punished for it, which is
what `harness.sh` `defect_ledger_intact` re-proves on every run (the two halves
partition the recording) and `defect_marker_tally` reports, unscored.

Output: both files as `json.dump(..., indent=1, sort_keys=True)` plus a
newline -- the shipped serialisation; gzipped, they are `data/expected.json.gz`
and `data/defects.json.gz`. Applied to a recording of the pinned tree it
reproduces the shipped scored set and ledger byte for byte once decompressed.
"""
import json
import sys

# marker text -> reason, in the order a body is tested against them.
MARKERS = [
    ("generator raised StopIteration",
     "PEP 479: bento/core/node.py:392 (`_ant_iter`) and bento/parser/lexer.py:502 "
     "(`merge_escaped`) end a generator with a bare `raise StopIteration`, which "
     "is a RuntimeError from Python 3.7 on. Every glob resolution and every "
     "empty document reaches one."),
    ("bentomaker crashed (uncaught exception",
     "bentomakerlib/bentomaker.py's top-level handler, reporting one of the "
     "PEP 479 sites above through the command line rather than the API."),
]


def freeze(recording):
    expected, defects = {}, {}
    for key in sorted(recording):
        body = recording[key]
        hit = next(((m, r) for m, r in MARKERS if m in body), None)
        if hit is None:
            expected[key] = body
        else:
            defects[key] = {"marker": hit[0], "observed": body, "reason": hit[1]}
    return expected, defects


def main(argv):
    with open(argv[1]) as fh:
        recording = json.load(fh)
    expected, defects = freeze(recording)
    for path, data in ((argv[2], expected), (argv[3], defects)):
        with open(path, "w") as fh:
            json.dump(data, fh, indent=1, sort_keys=True)
            fh.write("\n")
    sys.stderr.write("freeze: %d scored + %d ledgered = %d observations\n"
                     % (len(expected), len(defects), len(recording)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
