#!/usr/bin/env python3
"""Replay a case file through cpridrv, surviving cases that kill the driver.

    runner.py <driver> <cases> <out> [--stall SECONDS] [--quiet]
                                     [--errlog PATH]

--errlog keeps the driver's stderr instead of discarding it, which is what a
sanitizer build writes its reports to.

The driver flushes one result line per case, so if it dies the number of lines
already written says exactly which case killed it.  The runner records that
case with a synthetic result and restarts the driver just past it, which is
what keeps one bad case from silently deleting the rest of the corpus.

A driver that stops producing output for --stall seconds is treated the same
way as one that crashed: an expectation that never arrives is not a pass.

Exit status is 0 whatever the cases did; the result file is the output.  What
the cases were supposed to do is the caller's business, not the runner's --
this is glue, and every expectation comes from the baseline run.
"""
import os
import subprocess
import sys
import time


def read_cases(path):
    """[(line-number, case-id)] for the lines that produce a result."""
    out = []
    with open(path) as fh:
        for i, line in enumerate(fh):
            tok = line.split()
            if tok and not tok[0].startswith("#"):
                out.append((i, tok[0]))
    return out


def count_lines(path):
    if not os.path.exists(path):
        return 0
    with open(path) as fh:
        return sum(1 for _ in fh)


def run_from(driver, cases, out, first, stall, errlog=None):
    """Start the driver at case-file line `first`; return (rc, note).

    note is None for a clean exit, or a short reason when it was killed.
    """
    err = open(errlog, "ab") if errlog else subprocess.DEVNULL
    proc = subprocess.Popen([driver, cases, out, str(first)],
                            stdout=subprocess.DEVNULL, stderr=err)
    if errlog:
        err.close()
    last_n, last_t = count_lines(out), time.time()
    while True:
        rc = proc.poll()
        if rc is not None:
            return rc, None
        time.sleep(0.25)
        n = count_lines(out)
        if n != last_n:
            last_n, last_t = n, time.time()
        elif time.time() - last_t > stall:
            proc.kill()
            proc.wait()
            return None, "stalled for %ds" % stall


def main(argv):
    if len(argv) < 4:
        sys.stderr.write(__doc__)
        return 2
    driver, cases, out = argv[1], argv[2], argv[3]
    stall = 300.0
    quiet = "--quiet" in argv
    if "--stall" in argv:
        stall = float(argv[argv.index("--stall") + 1])
    errlog = argv[argv.index("--errlog") + 1] if "--errlog" in argv else None

    entries = read_cases(cases)
    if os.path.exists(out):
        os.remove(out)
    if errlog and os.path.exists(errlog):
        os.remove(errlog)

    first, restarts = 0, 0
    while True:
        rc, note = run_from(driver, cases, out, first, stall, errlog)
        n = count_lines(out)
        if n >= len(entries):
            break
        line, cid = entries[n]
        if note is None:
            # Python reports a killing signal as a negative return code.
            note = ("signal %d" % -rc) if rc is not None and rc < 0 \
                else "exit %s" % rc
        with open(out, "a") as fh:
            fh.write("%s died=1 how=%s\n" % (cid, note.replace(" ", "_")))
        if not quiet:
            sys.stderr.write("runner: %s took the driver down (%s); "
                             "resuming at line %d\n" % (cid, note, line + 1))
        restarts += 1
        first = line + 1
        if restarts > 200:
            sys.stderr.write("runner: too many restarts, giving up\n")
            return 1

    if not quiet:
        sys.stderr.write("runner: %d results, %d restart(s)\n"
                         % (n, restarts))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
