#!/usr/bin/env python3
"""Scorer for step 01-proto2.

Reads a normalized case list (/tests/cases.json) produced at authoring time and
runs each case against the candidate's /app/bin/protoc, comparing against
expectations recorded from the official protoc 32.1 release binary.

Design points that matter:
  * Every input and every expectation is read from /tests, which is baked into
    the verifier image. Nothing under /app is read except the rebuilt binary.
  * Each family scores as a fraction of its own case count, so a single failure
    costs a fraction of one family's weight rather than the step.
  * Prints exactly one line to stdout: the reward. Everything else goes to the
    log, so test.sh can capture the score by reading stdout.
"""
import argparse
import json
import os
import random
import re
import subprocess
import sys
import tempfile
import time
import shutil
import pwd
import stat


# --- unprivileged execution -------------------------------------------------
# Every program the candidate wrote -- protoc, conformance, protoc-gen-py,
# grpc_server -- runs as the unprivileged `candidate` account test.sh built it
# as, never as this root scorer. Run as root it could write into the pipe
# test.sh reads the score from, or rewrite the reward after the verifier has
# exited. There is no fallback: without that account nothing is run, and the
# step scores 0 with a VERIFIER DEFECT line in the log.
CANDIDATE_USER = "candidate"


def _candidate_account():
    try:
        pw = pwd.getpwnam(CANDIDATE_USER)
    except KeyError:
        return None
    if pw.pw_uid == 0 or os.geteuid() != 0:
        return None
    return pw


_CAND = _candidate_account()


def _drop_to_candidate():
    """preexec_fn: runs in the child, still root, once its stdio is in place.

    The child's own stdio pipes are handed to the account first, so it can
    reopen them by name (/dev/stdout, /dev/stdin) as any process can its own
    pipes; a root-owned pipe refuses that (EACCES), which would fail a
    `--descriptor_set_out=/dev/stdout` case. Only FIFOs: /dev/null and files
    are left alone. Then the drop itself; any failure raises, and nothing
    runs as root.
    """
    for fd in (0, 1, 2):
        try:
            if stat.S_ISFIFO(os.fstat(fd).st_mode):
                os.fchown(fd, _CAND.pw_uid, _CAND.pw_gid)
        except OSError:
            pass
    os.setgroups([])
    os.setgid(_CAND.pw_gid)
    os.setuid(_CAND.pw_uid)


# keyword arguments for subprocess.run / Popen that drop to that account
AS_CANDIDATE = ({"preexec_fn": _drop_to_candidate,
                 "env": dict(os.environ, HOME="/tmp")} if _CAND else {})


def give_to_candidate(path):
    """Let the candidate account write its outputs into a scorer-made dir."""
    if _CAND:
        os.chown(path, _CAND.pw_uid, _CAND.pw_gid)


def candidate_exec_prefix():
    """The same drop as an argv prefix, for a program a shell starts."""
    return ["setpriv", f"--reuid={_CAND.pw_uid}", f"--regid={_CAND.pw_gid}",
            "--clear-groups", "--"]
# --- end unprivileged execution ---------------------------------------------


# --- expectation guard ------------------------------------------------------
# /tests is world-readable, so a scored program could reach the expectations
# through a relative path such as "..". Load every expectation and every piped
# input into memory, then delete the expect* trees before any candidate code runs.
# After the delete, an expectation the loader missed is indistinguishable from a
# candidate that produced nothing -- so a miss is a VERIFIER DEFECT, never a
# silent case failure.
_BLOB = {}


def _load_expectations(tests_dir, cases):
    keys = ("expect", "stdin", "response_content")
    def walk(o):
        if isinstance(o, dict):
            for k, v in o.items():
                if k in keys and isinstance(v, str):
                    p = os.path.join(tests_dir, v)
                    if os.path.exists(p):
                        with open(p, "rb") as fh:
                            _BLOB[v] = fh.read()
                walk(v)
        elif isinstance(o, list):
            for v in o:
                walk(v)
    walk(cases)
    removed = []
    for root, dirs, _ in os.walk(tests_dir):
        for d in list(dirs):
            if d.startswith("expect"):
                shutil.rmtree(os.path.join(root, d), ignore_errors=True)
                removed.append(os.path.join(root, d))
                dirs.remove(d)
    # cases.json is relative-reachable from every scored cwd (../../cases.json).
    # It holds paths rather than answers, but the runner has already parsed it.
    cj = os.path.join(tests_dir, "cases.json")
    if os.path.exists(cj):
        try:
            os.unlink(cj)
            removed.append(cj)
        except OSError:
            pass
    return len(_BLOB), removed


def _blob(rel, log):
    if rel not in _BLOB:
        log(f"VERIFIER DEFECT: expectation {rel} was never loaded; refusing to score it")
        return None
    return _BLOB[rel]
# --- end expectation guard --------------------------------------------------


# --- refusal pairs ------------------------------------------------------------
# Nothing but a file's content may tell a refusal from its control: a cwd named
# `refuse`/`reject`, a file name describing the violation or a MANIFEST.tsv
# beside it would let a protoc refuse by context alone. So every refusal has a
# valid twin: the same file with the violation fixed and everything else
# (header, package, comments) unchanged, accepted by protoc 32.1.
# Both run the same way: each is copied into its own fresh scorer-made directory
# under a random name (same-directory imports are copied and renamed with it),
# with the same flags and `-I.` only, in random order. The case passes only if
# the invalid file is refused (non-zero exit, no signal, a diagnostic naming the
# file it was given) and its twin compiles. The source directories, MANIFEST.tsv
# included, are root-only from image build time; they are read into memory and
# deleted here before any candidate code runs.
_REFUSE = {}
_IMPORT_RE = re.compile(rb'(\bimport\s+(?:weak\s+|public\s+)?")([^"\n]+)(")')
_SYSRAND = random.SystemRandom()


def _closure(src_dir, main):
    files, seen, todo = [], set(), [main]
    while todo:
        rel = todo.pop(0)
        if rel in seen:
            continue
        seen.add(rel)
        with open(os.path.join(src_dir, rel), "rb") as fh:
            data = fh.read()
        files.append((rel, data))
        for m in _IMPORT_RE.finditer(data):
            dep = m.group(2).decode("utf-8", "replace")
            if "/" not in dep and os.path.isfile(os.path.join(src_dir, dep)):
                todo.append(dep)
    return files


def _load_refusals(tests_dir, spec, log):
    dirs = set()
    for fam in spec.get("families", []):
        for c in fam.get("cases", []):
            if c.get("exit") != "nonzero" or not c.get("twin"):
                continue
            src = os.path.join(tests_dir, c["cwd"])
            tw = os.path.join(tests_dir, c["twin"])
            try:
                _REFUSE[c["name"]] = (
                    _closure(src, c["argv"][-1]),
                    _closure(os.path.dirname(tw), os.path.basename(tw)))
            except OSError as exc:
                log(f"VERIFIER DEFECT: refusal pair {c['name']} could not be read: {exc}")
            dirs.add(src)
            dirs.add(os.path.dirname(tw))
    removed = 0
    for d in sorted(dirs):
        shutil.rmtree(d, ignore_errors=True)
        removed += not os.path.exists(d)
    return len(_REFUSE), removed, len(dirs)


def _run_staged(protoc, files, flags):
    d = tempfile.mkdtemp()
    try:
        names = {rel: "%016x.proto" % _SYSRAND.getrandbits(64) for rel, _ in files}

        def _sub(m):
            dep = m.group(2).decode("utf-8", "replace")
            return (m.group(1) + names[dep].encode() + m.group(3)) if dep in names else m.group(0)
        for rel, data in files:
            p = os.path.join(d, names[rel])
            with open(p, "wb") as fh:
                fh.write(_IMPORT_RE.sub(_sub, data))
            os.chmod(p, 0o644)
        give_to_candidate(d)
        main = names[files[0][0]]
        argv = [protoc, "-I."] + [a.replace("{TMP}", d) for a in flags] + [main]
        try:
            proc = subprocess.run(argv, cwd=d, input=b"", stdout=subprocess.PIPE,
                                  stderr=subprocess.PIPE, timeout=TIMEOUT, **AS_CANDIDATE)
        except subprocess.TimeoutExpired:
            return None, main, f"timeout after {TIMEOUT}s"
        except OSError as exc:
            return None, main, f"could not execute: {exc}"
        return proc, main, None
    finally:
        shutil.rmtree(d, ignore_errors=True)


def run_refusal_pair(protoc, tests_dir, case, log):
    pair = _REFUSE.get(case["name"])
    if not pair:
        log(f"VERIFIER DEFECT: refusal pair {case['name']} was never loaded; refusing to score it")
        return False
    flags = [a.replace("{TESTS}", tests_dir) for a in case["argv"][:-1]
             if not (a.startswith("-I") or a.startswith("--proto_path"))]
    order = ["refuse", "twin"]
    _SYSRAND.shuffle(order)
    res = {side: _run_staged(protoc, pair[0] if side == "refuse" else pair[1], flags)
           for side in order}
    proc, main, err = res["refuse"]
    if err:
        log(f"FAIL {case['name']}: invalid file: {err}")
        return False
    if proc.returncode == 0:
        log(f"FAIL {case['name']}: expected non-zero exit, got 0")
        return False
    # subprocess reports a signal as -N; 128..255 is an ordinary exit(2) status
    if proc.returncode < 0:
        log(f"FAIL {case['name']}: died on signal {-proc.returncode} instead of rejecting the file")
        return False
    msg = proc.stderr.decode("utf-8", "replace")
    if not msg.strip():
        log(f"FAIL {case['name']}: rejected but stderr was empty")
        return False
    if main not in msg:
        log(f"FAIL {case['name']}: diagnostic does not name the file it was given")
        return False
    tproc, _tmain, terr = res["twin"]
    if terr:
        log(f"FAIL {case['name']}: valid twin: {terr}")
        return False
    if tproc.returncode != 0:
        log(f"FAIL {case['name']}: rejected the invalid file, but its valid twin also "
            f"failed (exit {tproc.returncode})")
        return False
    return True
# --- end refusal pairs --------------------------------------------------------


TIMEOUT = 60
DEADLINE_SEC = 1800  # scorer must finish well inside the step's verifier budget


class Log:
    def __init__(self, path):
        self.f = open(path, "a", encoding="utf-8", errors="replace")

    def __call__(self, *a):
        print(*a, file=self.f)
        self.f.flush()


def run_case(protoc, tests_dir, case, log):
    """Run one case. Returns True on pass."""
    if case.get("twin"):
        return run_refusal_pair(protoc, tests_dir, case, log)
    workdir = os.path.join(tests_dir, case.get("cwd", "corpus"))
    with tempfile.TemporaryDirectory() as tmp:
        give_to_candidate(tmp)
        argv = [protoc] + [
            a.replace("{TMP}", tmp).replace("{TESTS}", tests_dir) for a in case["argv"]
        ]
        stdin_data = b""
        if case.get("stdin"):
            stdin_data = _blob(case["stdin"], log) or b""
        try:
            proc = subprocess.run(
                argv, cwd=workdir, input=stdin_data,
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=TIMEOUT,
                **AS_CANDIDATE,
            )
        except subprocess.TimeoutExpired:
            log(f"FAIL {case['name']}: timeout after {TIMEOUT}s")
            return False
        except OSError as exc:
            log(f"FAIL {case['name']}: could not execute: {exc}")
            return False

        want_exit = case.get("exit", 0)
        if want_exit == "nonzero":
            if proc.returncode == 0:
                log(f"FAIL {case['name']}: expected non-zero exit, got 0")
                return False
            # A crash is not a rejection. Refusing an invalid file and dying on
            # it are different behaviours, and "exit != 0" cannot tell them
            # apart -- a SIGSEGV would otherwise score as a correct refusal.
            # subprocess reports a signal as -N; 128..255 is an ordinary exit(2) status
            if proc.returncode < 0:
                log(f"FAIL {case['name']}: died on signal "
                    f"{-proc.returncode}"
                    f" instead of rejecting the file")
                return False
            # A rejection must also say something on stderr. The wording is the
            # implementation's own business and is not compared.
            err = proc.stderr.decode("utf-8", "replace")
            if not err.strip():
                log(f"FAIL {case['name']}: rejected but stderr was empty")
                return False
            # The instruction requires the diagnostic to name the offending
            # file, so a generic refusal does not count.
            base = os.path.basename(case["argv"][-1])
            if base not in err:
                log(f"FAIL {case['name']}: diagnostic does not name {base}")
                return False
            # ...and refusing everything is not the same as refusing THIS. A
            # known-valid control file must still compile, so a candidate that
            # exits non-zero on all input collects nothing here.
            control = case.get("control")
            if control:
                cargv = [protoc] + [
                    a.replace("{TMP}", tmp).replace("{TESTS}", tests_dir) for a in control
                ]
                try:
                    cproc = subprocess.run(
                        cargv, cwd=workdir, input=b"", stdout=subprocess.PIPE,
                        stderr=subprocess.PIPE, timeout=TIMEOUT, **AS_CANDIDATE)
                except (subprocess.TimeoutExpired, OSError) as exc:
                    log(f"FAIL {case['name']}: control file did not run: {exc}")
                    return False
                if cproc.returncode != 0:
                    log(f"FAIL {case['name']}: rejected the invalid file, but the "
                        f"valid control file also failed (exit {cproc.returncode})")
                    return False
            return True

        if proc.returncode != want_exit:
            tail = proc.stderr.decode("utf-8", "replace").strip().splitlines()[-3:]
            log(f"FAIL {case['name']}: exit {proc.returncode} != {want_exit}: {tail}")
            return False

        if case.get("nonempty_stdout") and not proc.stdout.strip():
            log(f"FAIL {case['name']}: expected non-empty stdout")
            return False

        if "expect" not in case:
            return True

        want = _blob(case["expect"], log)
        if want is None:
            return False

        if case.get("compare", "stdout") == "stdout":
            got = proc.stdout
        else:
            out_path = case["outfile"].replace("{TMP}", tmp)
            if not os.path.exists(out_path):
                log(f"FAIL {case['name']}: expected output file {out_path} not written")
                return False
            with open(out_path, "rb") as fh:
                got = fh.read()

        if got != want:
            log(f"FAIL {case['name']}: {len(got)} bytes differ from expected {len(want)}")
            for i in range(min(len(got), len(want))):
                if got[i] != want[i]:
                    lo, hi = max(0, i - 12), i + 12
                    log(f"       first difference at byte {i}")
                    log(f"       expected {want[lo:hi].hex()}")
                    log(f"       got      {got[lo:hi].hex()}")
                    break
            return False
        return True


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--app", default="/app")
    ap.add_argument("--tests", default="/tests")
    ap.add_argument("--log", default="/logs/verifier/step01.log")
    args = ap.parse_args()

    log = Log(args.log)
    if not _CAND:
        log(f"VERIFIER DEFECT: no unprivileged '{CANDIDATE_USER}' account to run the "
            "candidate's programs as; refusing to run them as root -- reward 0")
        print("0.000000")
        return 0
    protoc = os.path.join(args.app, "bin", "protoc")

    with open(os.path.join(args.tests, "cases.json"), encoding="utf-8") as fh:
        spec = json.load(fh)
    _nr, _rmd, _nd = _load_refusals(args.tests, spec, log)
    log(f"refusal pairs: {_nr} held in memory, {_rmd}/{_nd} source directories "
        f"removed from /tests")
    _n, _rm = _load_expectations(args.tests, spec)
    log(f"expectation guard: {_n} blobs held in memory, {len(_rm)} paths removed from /tests")

    total = 0.0
    started = time.monotonic()
    log(f"scoring against protoc {spec.get('protoc')} expectations")
    for fam in spec["families"]:
        cases = fam["cases"]
        passed = 0
        ran = 0
        for c in cases:
            if time.monotonic() - started > DEADLINE_SEC:
                log(f"DEADLINE reached after {ran}/{len(cases)} of family {fam['id']}")
                break
            ran += 1
            try:
                _ok = run_case(protoc, args.tests, c, log)
            except Exception as exc:
                # Whatever the candidate's programs wrote must cost this one case,
                # never the step: an uncaught exception would end the scorer.
                log(f"FAIL {c.get('name')}: scorer could not evaluate this case "
                    f"({type(exc).__name__}: {exc}); the case fails")
                _ok = False
            if _ok:
                passed += 1
        # Score against the full set, never against the subset that happened to
        # run: a deadline must cost credit, not silently rescale it.
        frac = passed / len(cases) if cases else 0.0
        log(f"family {fam['id']}: ran {ran} of {len(cases)}")
        contrib = frac * fam["weight"]
        total += contrib
        log(f"family {fam['id']:<10} {passed:4d}/{len(cases):<4d} = {frac:.4f}"
            f"  weight {fam['weight']:.2f}  contributes {contrib:.4f}")

    total = max(0.0, min(1.0, total))
    log(f"TOTAL reward {total:.6f}")
    print(f"{total:.6f}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
