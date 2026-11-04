#!/usr/bin/env python3
"""Digest the source tree, as root, before the candidate's build touches it.

    snapshot_tree.py <src> <out>

Called from helpers.sh between the stale-output sweep and the configure. The
comparison happens in isefix.gates.run_gates, which reads the file this writes
out of ISEFIX_TREE_SNAPSHOT; see snapshot_tree in helpers.sh for what the
bracket is for and why it starts after the sweep rather than before it.

It is a separate script rather than a line of python3 -c in helpers.sh so that
the file set is gates.walk's and stays gates.walk's: the digest is only
evidence if both sides of the comparison agree about which files are the
tree's, and the way to guarantee that is for there to be one definition.

Never fatal. A snapshot that could not be taken leaves a file that is not
there, which run_gates refuses in the same breath as a missing trace -- the
verifier must not be able to turn a broken snapshot into a green run, and must
not be able to turn it into a failed build either.
"""
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from isefix import gates                                  # noqa: E402


def main():
    if len(sys.argv) != 3:
        print("usage: snapshot_tree.py <src> <out>", file=sys.stderr)
        return 2
    src, out = sys.argv[1], sys.argv[2]
    digest = gates.tree_digest(src)
    with open(out, "w") as handle:
        json.dump(digest, handle, indent=0, sort_keys=True)
    os.chmod(out, 0o600)
    print("snapshot of %s: %d file(s) -> %s" % (src, len(digest), out))
    # The suppressions in what walk prunes, read now -- before the configure,
    # when everything under build/, debug/ and the rest is still the
    # candidate's own writing rather than the build's output. run_gates reads
    # this beside the digest; see gates.authored_suppressions.
    hits = gates.authored_suppressions(src)
    with open(out + ".suppressions", "w") as handle:
        json.dump(hits, handle, indent=0)
    os.chmod(out + ".suppressions", 0o600)
    print("suppressions under pruned directories before the configure: %d"
          % len(hits))
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as exc:                              # noqa: BLE001
        print("snapshot failed: %r" % (exc,), file=sys.stderr)
        sys.exit(1)
