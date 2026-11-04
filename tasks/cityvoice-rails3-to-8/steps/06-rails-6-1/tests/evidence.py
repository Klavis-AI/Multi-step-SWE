"""Collects a step's probe reports into the one object the scorer reads.

`test.sh` runs half a dozen instruments -- the behaviour comparison and the
five gate probes -- and each writes its own JSON.  `score.py` evaluates gates
against *one* object addressed by dotted paths, so something has to join them.
Doing it here rather than with `jq` in the shell buys three things, each of
which a shell join would get wrong:

  * A probe that did not run is recorded as `{"error": ...}`, not omitted.
    `score.py` reads that shape as "this probe could not be asked" and fails
    the gates over it while leaving the others measuring.  An omitted key would
    do the same thing here, but it would also be indistinguishable from a gate
    whose path was mistyped -- and the mistyped one has to be findable.
  * Unparseable output is a recorded error too.  A gate probe that boots a
    candidate's application can print anything at all to stdout; if it also
    truncated its report, `json.load` raises, and the honest report of that is
    "the instrument failed", not a crashed verifier.
  * The join is asserted.  Every key asked for appears in the output, every
    prune says whether it found anything, and the summary the verifier prints
    names each source file with its size -- so "the evidence was assembled" is
    a set of numbers in the log rather than an inference from a reward.

Nothing here interprets a report.  Renaming, defaulting or flattening a probe's
keys would put the meaning of the evidence in two places: this file and the
step's profile.  The profile is the only place a step's requirements live,
so this file stays a dumb merge and the paths in the profile
are the probes' own.
"""

from __future__ import annotations

import argparse
import json
import os
import sys


def _pair(text, flag):
    if "=" not in text:
        raise SystemExit("%s wants KEY=VALUE, got %r" % (flag, text))
    key, _, value = text.partition("=")
    if not key:
        raise SystemExit("%s wants a non-empty key, got %r" % (flag, text))
    return key, value


def load_report(path):
    """The JSON at `path`, or an error stub explaining why not.

    The stub is deliberately two keys at most: `score.py` treats a dict with an
    `error` key and no more than two keys as a failed probe, and a longer stub
    would be scored as if it were a report.
    """
    if not os.path.exists(path):
        return {"error": "no report at %s" % path}
    try:
        size = os.path.getsize(path)
    except OSError as exc:
        return {"error": "cannot stat %s: %s" % (path, exc)}
    if size == 0:
        # Its own case because it is the common one: a probe that died on its
        # first line leaves the file its `--out` created, empty.  "Expecting
        # value: line 1 column 1" is a true but unhelpful account of that.
        return {"error": "empty report at %s" % path}
    try:
        with open(path, "r", encoding="utf-8") as handle:
            return json.load(handle)
    except (ValueError, OSError) as exc:
        return {"error": "unreadable report at %s: %s" % (path, exc)}


def prune(evidence, path):
    """Drop the node at a dotted path.  Returns whether there was one.

    Used for the bulky per-case and per-finding lists: the behaviour comparison
    carries 386 case results and the source scan carries every finding with its
    file and line, and both are written to the verifier's log directory in full.
    Keeping them in the evidence as well would multiply the object the scorer
    loads by fifty for no gate.
    """
    segments = path.split(".")
    node = evidence
    for segment in segments[:-1]:
        if not isinstance(node, dict) or segment not in node:
            return False
        node = node[segment]
    if not isinstance(node, dict) or segments[-1] not in node:
        return False
    del node[segments[-1]]
    return True


def assemble(files, prunes=(), meta=None):
    """`[(key, path)]` -> the merged object, plus its own account of itself."""
    evidence = {}
    sources = {}
    for key, path in files:
        if key in evidence:
            raise SystemExit("evidence: %r given twice" % key)
        report = load_report(path)
        evidence[key] = report
        sources[key] = {
            "path": path,
            "bytes": os.path.getsize(path) if os.path.exists(path) else None,
            "error": report.get("error") if isinstance(report, dict) else None,
        }
    pruned = {path: prune(evidence, path) for path in prunes}
    evidence["meta"] = dict(meta or {})
    evidence["meta"]["sources"] = sources
    evidence["meta"]["pruned"] = pruned
    return evidence


def render(evidence):
    lines = []
    sources = evidence["meta"]["sources"]
    for key in sorted(sources):
        entry = sources[key]
        lines.append("  %-12s %9s bytes  %s%s"
                     % (key,
                        "-" if entry["bytes"] is None else entry["bytes"],
                        entry["path"],
                        "   FAILED: %s" % entry["error"]
                        if entry["error"] else ""))
    for path, found in sorted(evidence["meta"]["pruned"].items()):
        if not found:
            # Not an error: a probe that failed has no bulky list to drop.  It
            # is printed because the other reason for it is that a report's
            # shape changed under a prune nobody updated.
            lines.append("  prune %s matched nothing" % path)
    failed = sorted(k for k, v in sources.items() if v["error"])
    lines.append("  %d report%s, %d failed%s"
                 % (len(sources), "" if len(sources) == 1 else "s",
                    len(failed), ": %s" % failed if failed else ""))
    return "\n".join(lines)


def main(argv=None):
    parser = argparse.ArgumentParser(prog="evidence")
    parser.add_argument("--file", action="append", default=[], metavar="KEY=PATH",
                        help="a probe report, merged under KEY")
    parser.add_argument("--prune", action="append", default=[], metavar="PATH",
                        help="dotted path to drop from the merged object")
    parser.add_argument("--meta", action="append", default=[], metavar="KEY=VALUE",
                        help="literal string recorded under meta")
    parser.add_argument("--out", required=True)
    args = parser.parse_args(argv)

    files = [_pair(text, "--file") for text in args.file]
    meta = dict(_pair(text, "--meta") for text in args.meta)
    evidence = assemble(files, args.prune, meta)
    with open(args.out, "w", encoding="utf-8") as handle:
        json.dump(evidence, handle, indent=1, sort_keys=True)
        handle.write("\n")
    print(render(evidence))
    # Always 0.  Every failure this file can see is a failure it has recorded
    # for the scorer to act on; exiting non-zero would turn a broken probe into
    # a crashed step, which is the one outcome `mean` cannot represent.
    return 0


if __name__ == "__main__":
    sys.exit(main())
