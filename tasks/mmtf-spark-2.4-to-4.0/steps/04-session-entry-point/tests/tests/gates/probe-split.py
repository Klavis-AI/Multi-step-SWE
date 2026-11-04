#!/usr/bin/env python3
"""Split the replay's probe stream into one file per job, as root.

The replay runs as uid 65534 and so does every job in it, which means the
runner cannot own the files it writes. Round 3 of the item-5 review followed
that through: the per-job fifo took the *path* out of the job's environment,
but the file the runner drained it into still landed in a directory the same
uid could reach, and every rule over the per-program kinds is `absent` -- so a
later job that emptied, relabelled or rewrote an earlier job's file turned a
red reading into no reading at all, which those rules read as clean.

So the drain moves to this side of the wall. Root makes one fifo, mode 0222
inside a 0700 directory -- writable through an inherited descriptor, not
findable by path -- and runs this script on it for the length of the replay.
The jobs write; root reads; the files never exist anywhere the candidate's uid
can name.

That leaves one question, which is the whole reason this is not a `cat`: with
one pipe for 140 jobs, whose rows are these? Not the runner's word for it --
the runner's word arrives on the same pipe and could be forged by anything
holding the descriptor. Each job is given a fresh random token in
GATE_PROBE_NONCE, the listener puts it in front of every row it writes
(GateProbe.java), and the runner records the token against the job's name on a
second descriptor the job does not inherit. A process that outlives its own
job still holds the pipe and can still write to it -- but it knows its own
token and not the next job's, so its rows land in its own file, beside the real
ones, where a repeated key is already `tampered` and priced at the whole
reward (cheats4.tsv). Rows with a token nobody was issued are nobody's: they
are kept, counted, and reported under the label `stray`, which is a rule of its
own.

Two modes, because the split runs while the jobs do and the naming cannot:

  drain     --fifo F --blocks D --stop T [--ready R]
            Read until the stop line carries token T -- which root writes and
            no other process has seen -- appending each row to D/<token>.tsv.
            Append and flush, so a drain that is killed keeps what it read.
            R, if given, is created once the fifo is open for reading: the
            caller's own open for writing blocks until a reader arrives, and a
            caller that waits on this file instead can give up on a drain that
            never started rather than hang the verifier on it.

  assemble  --blocks D --ctl F --out O
            After the replay: read the runner's token-to-job table and rename
            each block to the job that owned the token. Blocks with no entry
            in the table, and rows that carried no usable token, become
            O/stray.tsv.
"""
import os
import re
import sys

NONCE = re.compile(r'^[0-9a-f]{16,64}$')
NAME = re.compile(r'[^A-Za-z0-9._-]')
STOP = '__probe-split-stop__'
STRAY = '_stray.tsv'


def drain(fifo, blocks, stop, ready=None):
    """Read the shared pipe until root says stop; one file per token."""
    # The stop line is a row like any other on this pipe, so a token anything
    # writing on it could produce -- an empty one, most of all, which is what a
    # caller that lost the variable would pass -- would be a way to end the
    # reading early and leave every job after it silent.
    if not NONCE.match(stop):
        sys.stderr.write('probe-split: the stop token must be 16-64 hex '
                         'digits, not %r\n' % stop)
        return 2
    os.makedirs(blocks, mode=0o700, exist_ok=True)
    # Read-write, so the reader never sees EOF between one job closing the
    # pipe and the next one opening it. The runner holds a writing descriptor
    # for the whole replay as well; this is the belt to that suspenders, and it
    # is what lets this script start before the runner does.
    fd = os.open(fifo, os.O_RDWR)
    if ready:
        with open(ready, 'w') as fh:
            fh.write('%d\n' % os.getpid())
    handles = {}
    rows, strays = 0, 0
    try:
        with os.fdopen(fd, 'r', buffering=1, errors='replace') as stream:
            for line in stream:
                line = line.rstrip('\n')
                if line == '%s\t%s' % (STOP, stop):
                    break
                token, _, rest = line.partition('\t')
                if NONCE.match(token) and rest:
                    name, out = token + '.tsv', rest
                else:
                    # Including a line that is only a token, and a line torn in
                    # half by two writers: neither is a row this gate can read,
                    # and dropping it silently is the one thing that must not
                    # happen to a stream whose whole purpose is that rows
                    # cannot be unsaid.
                    name, out, strays = STRAY, line, strays + 1
                if name not in handles:
                    handles[name] = open(os.path.join(blocks, name), 'a')
                handles[name].write(out + '\n')
                handles[name].flush()
                rows += 1
    finally:
        for h in handles.values():
            h.close()
    sys.stderr.write('probe-split: %d row(s) in %d block(s), %d stray\n'
                     % (rows, len([k for k in handles if k != STRAY]), strays))
    return 0


def assemble(blocks, ctl, out):
    """Name each block after the job whose token it carries."""
    os.makedirs(out, mode=0o755, exist_ok=True)
    jobs = {}
    if os.path.exists(ctl):
        with open(ctl, errors='replace') as fh:
            for line in fh:
                token, _, job = line.rstrip('\n').partition('\t')
                if NONCE.match(token) and job:
                    jobs[token] = NAME.sub('_', job)
    named, stray = 0, []
    for name in sorted(os.listdir(blocks) if os.path.isdir(blocks) else []):
        src = os.path.join(blocks, name)
        token = name[:-4] if name.endswith('.tsv') else name
        job = jobs.get(token)
        if job is None:
            stray.append(src)
            continue
        os.rename(src, os.path.join(out, job + '.tsv'))
        os.chmod(os.path.join(out, job + '.tsv'), 0o644)
        named += 1
    if stray:
        # One file, under a label no job has and no plan can produce, so the
        # rule that prices it reads the same row however many strays there
        # were. It is not merged into any job's file: attributing somebody's
        # forgery to a program would be the same mistake in the other
        # direction.
        with open(os.path.join(out, 'stray.tsv'), 'w') as fh:
            fh.write('probe\tapp.label\tstray\n')
            for src in stray:
                with open(src, errors='replace') as sf:
                    fh.write(sf.read())
        os.chmod(os.path.join(out, 'stray.tsv'), 0o644)
    sys.stdout.write('probe-split: %d job block(s), %d stray file(s)\n'
                     % (named, len(stray)))
    return 0


def main(argv):
    if not argv:
        sys.stderr.write(__doc__)
        return 2
    mode, argv = argv[0], argv[1:]
    opts = {}
    while argv:
        if not argv[0].startswith('--') or len(argv) < 2:
            sys.stderr.write('probe-split: want --option value, got %s\n' % argv[0])
            return 2
        opts[argv[0][2:]] = argv[1]
        argv = argv[2:]
    try:
        if mode == 'drain':
            return drain(opts['fifo'], opts['blocks'], opts['stop'],
                         opts.get('ready'))
        if mode == 'assemble':
            return assemble(opts['blocks'], opts['ctl'], opts['out'])
    except KeyError as missing:
        sys.stderr.write('probe-split: %s needs --%s\n' % (mode, missing.args[0]))
        return 2
    sys.stderr.write('probe-split: unknown mode %s\n' % mode)
    return 2


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
