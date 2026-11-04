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
Milestone 1 (pdf13-core):
info      run `pdfinfo ARGS FILE`, compare the scored field subset of stdout
text      run `pdftotext ARGS FILE -`, compare after normalization
fonts     run `pdffonts ARGS FILE`, compare the normalized table
stream    run `qpdf --show-object=N[,G] --filtered-stream-data FILE`, compare sha256
raw       run `qpdf --show-object=N[,G] --raw-stream-data FILE`, compare sha256
exit      run any of the above, compare only the exit status

Milestone 2 (pdf14-transparency), one per comparison form in TEST_INTERFACE.md 4.1:
pages     `qpdf --show-pages`, compared as a normalized line sequence
xref      `qpdf --show-xref`, compared as a normalized line sequence
object    `qpdf --show-object=...`, whole dump with whitespace runs collapsed
check     `qpdf --check`, only the four scored line forms, wrapped lines joined
linparams `qpdf --show-linearization`, the parameter block alone
lindump   `qpdf --show-linearization`, the full dump minus the per-page hint entries
warn      `qpdf --check`, the set of warning identities over stdout and stderr merged

Every case carries its own `expect`; nothing is derived here at scoring time, and the
recorded `expect_exit` is compared for every case regardless of kind.

The file operand
----------------
Every expectation in $TESTS was recorded by running the reference tool with the corpus
file's BARE BASENAME as the operand, and qpdf echoes that operand verbatim -- into
`checking FILE`, `FILE is not linearized`, `FILE: linearization data:` and each
`WARNING: FILE: ...` prefix. This runner must pass a path, so the echoed operand is
made path-independent before comparison; see strip_operand_path().
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

# --- milestone 2 comparison constants (TEST_INTERFACE.md 4.1) ---------------
LIN_PARAM_KEYS = ["file_size", "first_page_object", "first_page_end", "npages",
                  "xref_zero_offset", "first_page", "H_offset", "H_length"]
CHECK_JOINED = ("No syntax or stream encoding errors found; the file may still "
                "contain errors that qpdf cannot detect")
# 4.1 check: "Wrapped lines are joined". qpdf hard-wraps that sentence and different
# builds wrap it in different places, so the sentence is matched with any whitespace
# run between any two of its words and rewritten to the one-line form. The pattern is
# the sentence and nothing else, so joining can only ever affect the sentence.
CHECK_SENTENCE = re.compile(r"\s+".join(re.escape(w) for w in CHECK_JOINED.split(" ")))
CHECK_LEADS = ("checking ", "PDF Version:", "No syntax or stream encoding errors found;")
CHECK_EXACT = ("File is linearized", "File is not linearized")
# 4.1 linearization dump: a top-level "Page N:" line opens a per-page hint-table block.
PAGE_BLOCK = re.compile(r"^Page \d+:\s*$")
_DIGITS = re.compile(r"\d+")


def _text(s):
    if isinstance(s, bytes):
        return s.decode("utf-8", "replace")
    return s


def collapse(s):
    """One whitespace run -> one space, leading and trailing whitespace removed."""
    return _WS.sub(" ", s).strip()


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


def strip_operand_path(s, operand, basename):
    """Make an echoed file operand path-independent.

    The expectations were captured with a bare basename as the operand; this runner
    passes CORPUS_DIR/basename. Without this, the 108 check / lindump / warn cases
    would all fail on the directory prefix alone -- a harness artefact that looks
    exactly like a broken candidate.

    Two rewrites, both confined to the directory prefix sitting directly in front of
    an occurrence of this case's own basename:

      1. the operand exactly as this runner spelled it, wherever it appears;
      2. any remaining occurrence of the basename preceded by a path prefix -- a run
         of non-whitespace ending in "/".

    (1) alone would only work for a tool that echoes our argv verbatim. (2) is what
    makes the rule hold when the candidate's own binary spells the operand some other
    way -- "./NAME", a ".."-relative path, a resolved symlink, a path this runner
    never constructed.

    Neither rewrite can mask a difference elsewhere: the basename itself is never
    altered, the match must end exactly at the basename, nothing without a "/"
    immediately before the basename is touched, a basename embedded in a longer token
    is not matched, and an operand naming some other file keeps its whole path and
    still fails. Applied to both sides of the comparison, so the comparison is
    path-independent rather than merely one-sided.
    """
    s = _text(s)
    if operand and operand != basename:
        s = s.replace(operand, basename)
    return re.sub(r"\S*/" + re.escape(basename), basename, s)


def normalize_lines(s):
    """4.1 pages / xref: strip and collapse each line, drop empties, keep order."""
    return "\n".join(c for c in (collapse(l) for l in _text(s).splitlines()) if c)


def normalize_object(s):
    """4.1 object: whitespace runs collapsed over the whole dump, ends stripped.

    Whole-dump rather than per-line, so the reference's line wrapping and the two
    spaces in the "Object is stream. Dictionary:" header do not matter. Key order,
    dropped nulls and dangling references, verbatim reals and surviving escapes are
    all left exactly as the tool printed them -- they are what the case tests.
    """
    return collapse(_text(s))


def normalize_check(s, operand, basename):
    """4.1 check: only the four scored line forms, wrapped lines joined first."""
    s = CHECK_SENTENCE.sub(CHECK_JOINED, strip_operand_path(s, operand, basename))
    out = []
    for line in s.splitlines():
        c = collapse(line)
        if c.startswith(CHECK_LEADS) or c in CHECK_EXACT:
            out.append(c)
    return "\n".join(out)


def normalize_lin_params(s):
    """4.1 linearization dump, parameter block alone: the eight scored keys."""
    got = {}
    for line in _text(s).splitlines():
        m = re.match(r"^([a-zA-Z_]+):\s*(.*)$", line.strip())
        if m and m.group(1) in LIN_PARAM_KEYS and m.group(1) not in got:
            got[m.group(1)] = collapse(m.group(2))
    return "\n".join(f"{k}: {got[k]}" for k in LIN_PARAM_KEYS if k in got)


def normalize_lin_dump(s, operand, basename):
    """4.1 linearization dump, full: everything except the per-page hint entries.

    A top-level "Page N:" line opens the Page Offsets Hint Table's per-page block;
    that line and the indented entries under it are dropped, and the first line that
    is not an indented continuation ends the block and is kept. The parameter block,
    the hint table's own parameters and the whole Shared Objects Hint Table -- whose
    "Shared Object N:" blocks are not per-page blocks -- are all compared.

    The per-page entries are dropped because no field model of ISO 32000-1 Table F.3
    reproduces the reference tool's values for them, so they cannot be scored; the
    expectations in $TESTS were recorded with this same rule applied.
    """
    s = strip_operand_path(s, operand, basename)
    out, skipping = [], False
    for line in s.splitlines():
        if PAGE_BLOCK.match(line.strip()) and not line.startswith(" "):
            skipping = True
            continue
        if skipping:
            if line[:1] in (" ", "\t") and line.strip():
                continue
            skipping = False
        c = collapse(line)
        if c:
            out.append(c)
    return "\n".join(out)


def warning_set(out, err, operand, basename):
    """4.1 warnings: the unordered, de-duplicated set of warning identities.

    The one comparison made over stdout and stderr merged, so it does not depend on
    which stream a warning arrives on. Identity: drop "WARNING:", drop a following
    "FILE: " or "FILE (offset D): " prefix, replace every digit run with "#",
    collapse. The operand path is stripped first, so that a directory prefix's own
    digits never reach the digit rule.
    """
    t = (strip_operand_path(out, operand, basename) + "\n"
         + strip_operand_path(err, operand, basename))
    pre = re.compile(r"^" + re.escape(basename) + r"(?: \(offset \d+\))?: ")
    seen = set()
    for line in t.splitlines():
        c = line.strip()
        if not c.startswith("WARNING:"):
            continue
        c = c[len("WARNING:"):].strip()
        c = pre.sub("", c)
        c = _DIGITS.sub("#", c)
        seen.add(collapse(c))
    return "\n".join(sorted(seen))


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
    base = case["file"]
    path = os.path.join(CORPUS_DIR, base)
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

    # A matching exit status says nothing about what is on stdout: one fixture exits 3
    # with a full dump, another exits 3 with stdout empty, and malformed input exits 2
    # with stdout empty. Content is therefore always compared from the output itself,
    # never inferred from rc, and an empty stdout is a real answer that fails unless
    # the recorded expectation is empty too.
    if kind == "text":
        got = normalize_text(out)
    elif kind == "info":
        got = info_fields(out, case["keys"])
    elif kind == "fonts":
        got = normalize_table(out)
    elif kind in ("stream", "raw"):
        got = hashlib.sha256(out).hexdigest()
    elif kind in ("pages", "xref"):
        got = normalize_lines(out)
    elif kind == "object":
        got = normalize_object(out)
    elif kind == "check":
        got = normalize_check(out, path, base)
    elif kind == "linparams":
        got = normalize_lin_params(out)
    elif kind == "lindump":
        got = normalize_lin_dump(out, path, base)
    elif kind == "warn":
        got = warning_set(out, err, path, base)
    else:
        return False, f"unknown kind {kind}"

    want = case["expect"]
    if kind in ("check", "lindump", "warn"):
        # The same rewrite on the recorded side, so the comparison is path-independent
        # in both directions. It is the identity on every expectation recorded with a
        # bare basename, which is all of them.
        want = strip_operand_path(want, path, base)

    if got == want:
        return True, ""
    return False, f"{kind} mismatch: got {got[:80]!r} want {want[:80]!r}"


# Cases carried forward from the earlier milestones (ids `REGRESSION:`) are scored as a
# factor of their own: groups.txt carries them as `regress:<group>`, and helpers.sh
# multiplies this milestone's weighted fraction by theirs (reward = T1 x T2).
REGRESS_PREFIX = "REGRESSION:"


def score_group(case):
    g = case["group"]
    return "regress:" + g if str(case.get("id", "")).startswith(REGRESS_PREFIX) else g


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
            g = score_group(case)
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
            g = score_group(case)
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
