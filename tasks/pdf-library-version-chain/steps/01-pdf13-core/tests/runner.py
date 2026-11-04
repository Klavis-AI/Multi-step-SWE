#!/usr/bin/env python3
"""Scored-case runner, shared by every step of pdf-library-version-chain.

Reads $TESTS/cases.json (the scored set) and drives the candidate's utilities,
appending one "<group> <passed> <total>" line per group to $LOG/groups.txt.

Nothing the candidate wrote is trusted: every expectation comes from $TESTS, which
test.sh has already made root-only and fingerprinted. The PDFs themselves are read
from $CORPUS_DIR, a world-readable copy, because the candidate's binaries run under an
unprivileged uid and must be able to open them while the expectations stay unreadable.

Case kinds
----------
info    run `pdfinfo ARGS FILE`, compare the scored field subset of stdout
text    run `pdftotext ARGS FILE -`, compare after normalization
fonts   run `pdffonts ARGS FILE`, compare the normalized table
stream  run `qpdf --show-object=N[,G] --filtered-stream-data FILE`, compare sha256
raw     run `qpdf --show-object=N[,G] --raw-stream-data FILE`, compare sha256
exit    run any of the above, compare only the exit status
Every case carries its own `expect`; nothing is derived here at scoring time.
"""
import concurrent.futures as futures
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import time
import unicodedata

TESTS = os.environ.get("TESTS", "/tests")
CORPUS_DIR = os.environ.get("CORPUS_DIR", "/run/corpus")
BUILD = os.environ.get("CAND_BUILD", "/app/build")
SCORE_UID = os.environ.get("SCORE_UID", "65534")

# The candidate's PATH is its own build directory and nothing else. The reference PDF
# tools are not installed in this image at all, but the image does carry python3 for
# the scorer itself -- and a twenty-line C program that execs an interpreter would
# otherwise satisfy "written in C and built by make". With nothing else on PATH there
# is no interpreter for a candidate binary to reach by name.
CAND_PATH = BUILD

_SETPRIV = shutil.which("setpriv")
if _SETPRIV is None:
    # Same reasoning as test.sh: a conditional drop that silently vanishes would run
    # every candidate binary as root. Refuse rather than downgrade.
    raise SystemExit("FATAL: setpriv unavailable; refusing to run candidate binaries as root")
_LAUNCH = os.environ.get("CAND_LAUNCH", "")
if not (_LAUNCH and os.path.isabs(_LAUNCH) and os.access(_LAUNCH, os.X_OK)):
    raise SystemExit("FATAL: verifier launcher unavailable; refusing to run candidate binaries")
_WS = re.compile(r"\s+")
_CTRL = re.compile(r"[\x00-\x08\x0b\x0c\x0e-\x1f\x7f]")


def normalize_text(s):
    """Comparison form for extracted text: the character sequence, not the layout.

    Two correct extractors disagree about where spaces and line breaks fall, which is
    a layout heuristic rather than a conformance question. They do agree on which
    characters the page contains and in what order, so that is what is compared. A
    wrong glyph, a missing ligature or a bad /ToUnicode still fails.
    """
    if isinstance(s, bytes):
        s = s.decode("utf-8", "replace")
    s = unicodedata.normalize("NFC", s)
    s = s.replace("­", "")
    s = _CTRL.sub(" ", s)
    return _WS.sub(" ", s).strip()


def normalize_table(s):
    if isinstance(s, bytes):
        s = s.decode("utf-8", "replace")
    rows = []
    for line in s.splitlines():
        line = line.strip()
        if not line or set(line) <= set("- "):
            continue
        rows.append(_WS.sub(" ", line))
    return "\n".join(rows)


def info_fields(s, keys):
    if isinstance(s, bytes):
        s = s.decode("utf-8", "replace")
    got = {}
    for line in s.splitlines():
        if ":" in line:
            k, v = line.split(":", 1)
            got.setdefault(k.strip(), _WS.sub(" ", v.strip()))
    return "\n".join(f"{k}: {got[k]}" for k in keys if k in got)


def run_tool(argv, timeout):
    env = {
        "PATH": CAND_PATH,
        "LC_ALL": "C.UTF-8",
        "HOME": "/tmp",
        "TMPDIR": "/tmp",
        # The task's one frozen-clock hook (reproducible-builds.org).
        "SOURCE_DATE_EPOCH": "1700000000",
    }
    # The verifier's launcher (tests/launch.c, built by test.sh) performs the same
    # drop as setpriv and then lets the utility start no other program and open no
    # inet socket.
    argv = [_LAUNCH, str(SCORE_UID)] + argv
    try:
        # Run from /app, the documented workspace root, so a utility that resolves a
        # data file relative to the working directory behaves here exactly as it did
        # under the agent's own `make && run-public-tests.sh`.
        p = subprocess.run(argv, capture_output=True, timeout=timeout, env=env,
                           cwd="/app", stdin=subprocess.DEVNULL)
        return p.returncode, p.stdout, p.stderr
    except subprocess.TimeoutExpired:
        return -9, b"", b"timeout"
    except FileNotFoundError:
        return -127, b"", b"not built"
    except Exception as exc:  # noqa: BLE001 - a broken candidate scores, never crashes us
        return -1, b"", str(exc).encode()


def judge(case, timeout):
    """Returns (passed, detail). Never raises."""
    kind = case["kind"]
    path = os.path.join(CORPUS_DIR, case["file"])
    argv = [os.path.join(BUILD, case["tool"])] + case.get("args", []) + [path]
    if case.get("stdout_operand"):
        argv.append("-")

    rc, out, err = run_tool(argv, timeout)

    exp_exit = case.get("expect_exit")
    if exp_exit is not None and rc != exp_exit:
        return False, f"exit {rc} != {exp_exit}"
    if kind == "exit":
        return True, ""
    if rc < 0:
        return False, f"rc={rc} {err[:60]!r}"

    if kind == "text":
        got = normalize_text(out)
    elif kind == "info":
        got = info_fields(out, case["keys"])
    elif kind == "fonts":
        got = normalize_table(out)
    elif kind in ("stream", "raw"):
        got = hashlib.sha256(out).hexdigest()
    else:
        return False, f"unknown kind {kind}"

    if got == case["expect"]:
        return True, ""
    return False, f"{kind} mismatch: got {got[:80]!r} want {case['expect'][:80]!r}"


def main():
    with open(os.path.join(TESTS, "cases.json")) as fh:
        cases = json.load(fh)
    log_dir = os.environ.get("LOG", "/logs/verifier")
    deadline = float(os.environ.get("CASE_DEADLINE", "1200"))
    per_case_timeout = float(os.environ.get("CASE_TIMEOUT", "25"))
    workers = int(os.environ.get("CASE_WORKERS", "7"))

    start = time.time()
    results = {}
    ran = {}          # cases that actually executed, as distinct from cases counted
    done = set()
    detail_lines = []

    def work(idx_case):
        idx, case = idx_case
        try:
            ok, detail = judge(case, per_case_timeout)
        except Exception as exc:  # noqa: BLE001
            ok, detail = False, f"runner error: {exc}"
        return idx, case, ok, detail

    with futures.ThreadPoolExecutor(max_workers=workers) as pool:
        futs = {pool.submit(work, ic): ic[0] for ic in enumerate(cases)}
        for fut in futures.as_completed(futs):
            idx, case, ok, detail = fut.result()
            done.add(idx)
            g = case["group"]
            passed, total = results.get(g, (0, 0))
            results[g] = (passed + (1 if ok else 0), total + 1)
            ran[g] = ran.get(g, 0) + 1
            if not ok:
                detail_lines.append(f"FAIL {g} {case['id']}: {detail}")
            if time.time() - start > deadline:
                for f2 in futs:
                    f2.cancel()
                break

    # A case that never ran still counts against its group, so a verifier that runs
    # out of time scores low rather than scoring nothing.
    for idx, case in enumerate(cases):
        if idx not in done:
            g = case["group"]
            passed, total = results.get(g, (0, 0))
            results[g] = (passed, total + 1)
            detail_lines.append(f"UNRUN {g} {case['id']}: deadline")

    os.makedirs(log_dir, exist_ok=True)
    with open(os.path.join(log_dir, "groups.txt"), "a") as fh:
        for g in sorted(results):
            p, t = results[g]
            fh.write(f"{g} {p} {t}\n")
    # `ran` beside `total`: a case that was counted but never executed is a different
    # failure from a case that executed and disagreed, and only this file tells them
    # apart. A scored set that silently never runs is the failure mode this catches.
    with open(os.path.join(log_dir, "ran.txt"), "a") as fh:
        for g in sorted(results):
            p, t = results[g]
            fh.write(f"{g} ran={ran.get(g, 0)} counted={t} passed={p}\n")
    with open(os.path.join(log_dir, "failures.txt"), "a") as fh:
        fh.write("\n".join(detail_lines[:4000]) + "\n")
    for g in sorted(results):
        p, t = results[g]
        print(f"{g}: {p}/{t} (ran {ran.get(g, 0)})")
    print(f"elapsed {time.time() - start:.1f}s over {len(cases)} cases", flush=True)


if __name__ == "__main__":
    main()
