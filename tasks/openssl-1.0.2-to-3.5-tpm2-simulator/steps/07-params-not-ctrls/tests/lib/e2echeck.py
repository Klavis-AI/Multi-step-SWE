#!/usr/bin/env python3
"""Known-answer control for an end-to-end recording.

    e2echeck.py <recording> [--min N] [--quiet]

Provenance.  The recording under control here is expected-e2e.txt, taken by
record-e2e.sh from the frozen baseline snapshot in build/baseline -- the TCG
TPM 2.0 reference simulator at https://github.com/stwagnr/tpm2simulator, commit
e45324eba268723d39856111e7933c5c76238481 (2017-10-15), linking the OpenSSL
1.0.2b vendored in that tree -- built by mkdrv.sh with -DSPEC_VERSION=116
-DCMAKE_BUILD_TYPE=None -DCMAKE_C_FLAGS="-O1 -g", over REPLAYS serial replays
(100 by default).  This control runs on every one of those replays and not only
on the one that is kept.  The answers it recomputes come from hashlib and from
e2e.py's own input lists, so nothing here draws on an RNG or a clock either.

The engine half has smoke.expected: seventy cases whose answers are fixed by
FIPS-197, SP 800-38A, RFC 2202, RFC 4231 and the NIST curve parameters, so the
driver is rehearsed against arithmetic before any recording made with it is
believed.  This half had no such control, and a recording nobody can check is
just a file.

Every `e2ehash/ok/hash-<alg>-<n>`, `e2ehashseq/ok/seq-<alg>-<i>` and
`e2eseqctx/ok/ctx-*` line carries the digest the simulator returned for a
message this suite chose.  A digest is the one answer in the whole corpus that
can be recomputed without asking the library under test, so this recomputes
every one of them with hashlib -- a different implementation, in a different
process, reached through a different OpenSSL than the 1.0.2 the simulator
statically links -- and requires them to agree.  The inputs come from e2e.py
itself (`msg`, `HASH_LENS`, `CHUNKINGS`, the `CTX_*` shapes) rather than from a
copy kept here: a control holding its own idea of what was hashed stops being a
control the moment one of those lists changes.

The e2eseqctx answers are the ones this control exists for.  They are digests
of a state that was written out as bytes, evicted, read back, sometimes twice,
and only then completed -- so "the simulator agreed with itself" would prove
nothing about them.  hashlib never relocates anything, and it still has to
produce the same digest.

A disagreement is not a regression to be scored, it is a reason to stop: it
means the recording was taken with an instrument that gets SHA wrong, and
nothing else measured with it can be trusted either.  Exit status is 0 when
every known answer is reproduced and 1 otherwise, with each disagreement named.
"""
import os
import sys

sys.dont_write_bytecode = True                 # no __pycache__ in /tests
import hashlib                                                          # noqa
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from e2e import (CHUNKINGS, CTX_AFTER_PRE, CTX_AFTER_TAIL, CTX_AGAIN_PRE,
                 CTX_AGAIN_TAIL, CTX_CHAIN_PRE, CTX_CHAIN_ROUNDS,
                 CTX_CHAIN_STEP, CTX_CHAIN_TAIL, CTX_CHUNKS, CTX_FORK_A,
                 CTX_FORK_B, CTX_LIVE_COPY, CTX_LIVE_ORIG, CTX_LIVE_PRE,
                 CTX_MOVE_TAIL, HASH_LENS, msg)                        # noqa

HASHLIB = {"SHA1": "sha1", "SHA256": "sha256", "SHA384": "sha384"}


def known():
    """(family, tag) -> the digest hashlib says the simulator owed us."""
    want = {}
    for alg, hl in HASHLIB.items():
        for n in HASH_LENS:
            want[("e2ehash", "hash-%s-%d" % (alg, n))] = \
                hashlib.new(hl, msg(n)).hexdigest()
        for i, chunks in enumerate(CHUNKINGS):
            data = b"".join(msg(n, j) for j, n in enumerate(chunks[:-1]))
            if chunks:
                data += msg(chunks[-1], len(chunks) - 1)
            want[("e2ehashseq", "seq-%s-%d" % (alg, i))] = \
                hashlib.new(hl, data).hexdigest()

        # The relocation session.  Each shape is "what the state had eaten by
        # the time it was saved" followed by "what it ate afterwards", and the
        # digest is over the concatenation however many times it moved.
        def dig(*parts):
            return hashlib.new(hl, b"".join(msg(n, t) for n, t in parts)) \
                          .hexdigest()

        for n in CTX_CHUNKS:
            want[("e2eseqctx", "ctx-move-%s-%d" % (alg, n))] = \
                dig((n, 0), CTX_MOVE_TAIL)
            want[("e2eseqctx", "ctx-fork-a-%s-%d" % (alg, n))] = \
                dig((n, 0), *CTX_FORK_A)
            want[("e2eseqctx", "ctx-fork-b-%s-%d" % (alg, n))] = \
                dig((n, 0), *CTX_FORK_B)

        want[("e2eseqctx", "ctx-chain-%s" % alg)] = dig(
            (CTX_CHAIN_PRE, 0),
            *[(CTX_CHAIN_STEP, r) for r in range(CTX_CHAIN_ROUNDS)],
            CTX_CHAIN_TAIL)
        want[("e2eseqctx", "ctx-live-copy-%s" % alg)] = \
            dig((CTX_LIVE_PRE, 0), *CTX_LIVE_COPY)
        want[("e2eseqctx", "ctx-live-orig-%s" % alg)] = \
            dig((CTX_LIVE_PRE, 0), CTX_LIVE_ORIG)
        # One answer, asked of the original and of the copy: same prefix, same
        # tail, and the copy is only finished after the original already was.
        for who in ("orig", "copy"):
            want[("e2eseqctx", "ctx-after-complete-%s-%s" % (who, alg))] = \
                dig((CTX_AFTER_PRE, 0), CTX_AFTER_TAIL)

        # One blob, loaded and finished twice over.  SHA-256 only: the shape is
        # the question, and asking it of three algorithms would be three
        # spellings of one case.
        if alg == "SHA256":
            for i in range(2):
                want[("e2eseqctx", "ctx-load-again-%d" % i)] = \
                    dig(CTX_AGAIN_PRE, CTX_AGAIN_TAIL)
    return want


def main(argv):
    if len(argv) < 2:
        sys.stderr.write(__doc__)
        return 2
    quiet = "--quiet" in argv
    lo = int(argv[argv.index("--min") + 1]) if "--min" in argv else 191

    want = known()
    seen, bad = 0, []
    for line in open(argv[1]):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        parts = line.split()
        cid, fields = parts[0], dict(p.split("=", 1) for p in parts[1:]
                                     if "=" in p)
        family = cid.split("/")[0]
        key = (family, fields.get("t", ""))
        if key not in want:
            continue
        seen += 1
        got = fields.get("out", "")
        if got != want[key]:
            bad.append("MISMATCH %s t=%s\n  recorded %s\n  hashlib  %s"
                       % (cid, fields["t"], got or "(absent)", want[key]))

    for b in bad:
        print(b)
    if bad:
        print("e2echeck: %d of %d known answers disagree with hashlib"
              % (len(bad), seen))
        return 1
    # A control that matched nothing is not a control that passed.  Absent
    # cases are the failure mode this catches: a session that died before its
    # first digest leaves a recording with no digests in it at all, and
    # "0 of 0 disagree" would otherwise read as a clean bill of health.
    if seen < lo:
        print("e2echeck: only %d known answers were present, expected at "
              "least %d -- the recording is short, not clean" % (seen, lo))
        return 1
    if not quiet:
        print("e2echeck: %d known answers reproduced (hashlib)" % seen)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
