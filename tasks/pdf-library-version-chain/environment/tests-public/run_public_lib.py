#!/usr/bin/env python3
"""Comparison logic for the self-check.

Uses the same normalization as the milestone's comparisons: extracted text is compared as
a character sequence (NFC, control characters dropped, whitespace runs collapsed), not
as a layout, and the pdffonts table is compared row by row with runs of spaces
collapsed. The utilities are invoked exactly as their man pages define them.
"""
import hashlib, json, os, re, subprocess, unicodedata

HERE = os.path.dirname(os.path.abspath(__file__))
CORPUS = os.environ.get("CORPUS_DIR", os.path.join(HERE, "corpus"))
BUILD = os.environ.get("CAND_BUILD", "/app/build")

_WS = re.compile(r"\s+")
_CTRL = re.compile(r"[\x00-\x08\x0b\x0c\x0e-\x1f\x7f]")


def normalize_text(s):
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


def load_cases():
    with open(os.path.join(HERE, "cases.json")) as fh:
        return json.load(fh)


def judge(case, timeout=30):
    argv = [os.path.join(BUILD, case["tool"])] + case.get("args", []) \
           + [os.path.join(CORPUS, case["file"])]
    if case.get("stdout_operand"):
        argv.append("-")
    # Drive the utilities exactly as TEST_INTERFACE.md specifies: same working
    # directory, same restricted PATH, same locale and frozen clock. A harness that ran
    # them with the shell's own environment could pass here and still fail under that
    # environment, which is the worst kind of progress signal.
    env = {
        "PATH": BUILD,
        "LC_ALL": "C.UTF-8",
        "HOME": "/tmp",
        "TMPDIR": "/tmp",
        "SOURCE_DATE_EPOCH": "1700000000",
    }
    try:
        p = subprocess.run(argv, capture_output=True, timeout=timeout, env=env,
                           cwd="/app", stdin=subprocess.DEVNULL)
        rc, out, err = p.returncode, p.stdout, p.stderr
    except subprocess.TimeoutExpired:
        return False, "timeout"
    except FileNotFoundError:
        return False, f"{case['tool']} not built"

    exp_exit = case.get("expect_exit")
    if exp_exit is not None and rc != exp_exit:
        return False, f"exit {rc} != {exp_exit}"
    kind = case["kind"]
    if kind == "exit":
        return True, ""
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
    return False, f"got {got[:100]!r} want {case['expect'][:100]!r}"
