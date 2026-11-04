#!/usr/bin/env python3
"""Scorer for step 08-codegen.

Reads a normalized case list (/tests/cases.json) produced at authoring time and
runs each case against the candidate's /app/bin/protoc and /app/bin/conformance,
comparing against expectations recorded from the official protoc 32.1 release
binary and against the upstream conformance suite.

Design points that matter:
  * Every input and every expectation is read from /tests, which is baked into
    the verifier image. Nothing under /app is read except the rebuilt binaries.
  * Each family scores as a fraction of its own case count, so a single failure
    costs a fraction of one family's weight rather than the step.
  * Prints exactly one line to stdout: the reward. Everything else goes to the
    log, so test.sh can capture the score by reading stdout.
"""
import argparse
import json
import os
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


TIMEOUT = 60
DEADLINE_SEC = 1800          # scorer must finish well inside the step's budget
CONFORMANCE_TIMEOUT = 600
RUNNER = "/opt/verifier/bin/conformance_test_runner"
SCHEMA_DIR = "/opt/verifier/proto"


class Log:
    def __init__(self, path):
        self.f = open(path, "a", encoding="utf-8", errors="replace")

    def __call__(self, *a):
        print(*a, file=self.f)
        self.f.flush()


# --- CodeGeneratorResponse parsing -------------------------------------------
# Byte-comparing a serialized response would pin a field ORDER that plugin.proto
# does not fix. So the response is parsed and compared semantically: no error
# field, and the named file carries exactly the recorded bytes.
def _cgr_varint(b, i):
    v = s = 0
    while True:
        c = b[i]; i += 1
        v |= (c & 0x7F) << s; s += 7
        if not c & 0x80:
            return v, i


def _cgr_records(b):
    i = 0
    while i < len(b):
        tag, i = _cgr_varint(b, i)
        num, wt = tag >> 3, tag & 7
        if wt == 0:
            _, i = _cgr_varint(b, i); yield num, None
        elif wt == 1:
            yield num, b[i:i + 8]; i += 8
        elif wt == 5:
            yield num, b[i:i + 4]; i += 4
        elif wt == 2:
            n, j = _cgr_varint(b, i); yield num, b[j:j + n]; i = j + n
        else:
            raise ValueError("wire type %d" % wt)


def run_direct_plugin_case(tests_dir, case, log, fill):
    """Run a plugin BINARY directly on a recorded CodeGeneratorRequest.

    Every other codegen case compares bytes at an output
    path, which a protoc generating Python in-process satisfies without ever
    executing /app/bin/protoc-gen-py. The instruction's central claim is that
    code generation is split across two programs talking over a pipe, and only
    invoking the plugin on its own can hold that claim to account.
    """
    exe = fill(case["exec"])
    if not os.path.exists(exe):
        log(f"FAIL {case['name']}: {exe} does not exist")
        return False
    if not os.access(exe, os.X_OK):
        log(f"FAIL {case['name']}: {exe} is not executable")
        return False
    req = _blob(case["stdin"], log) or b""
    try:
        proc = subprocess.run([exe], input=req, stdout=subprocess.PIPE,
                              stderr=subprocess.PIPE, timeout=TIMEOUT, **AS_CANDIDATE)
    except subprocess.TimeoutExpired:
        log(f"FAIL {case['name']}: plugin timed out after {TIMEOUT}s")
        return False
    except OSError as exc:
        log(f"FAIL {case['name']}: could not execute {exe}: {exc}")
        return False
    if proc.returncode != 0:
        tail = proc.stderr.decode("utf-8", "replace").strip().splitlines()[-2:]
        log(f"FAIL {case['name']}: plugin exited {proc.returncode}: {tail}")
        return False
    try:
        recs = list(_cgr_records(proc.stdout))
    except Exception as exc:
        log(f"FAIL {case['name']}: response did not parse: {exc}")
        return False
    errs = [v for n, v in recs if n == 1]
    if errs:
        _e0 = errs[0]
        log(f"FAIL {case['name']}: plugin reported error "
            f"{_e0.decode('utf-8', 'replace') if isinstance(_e0, bytes) else '(not a string)'!r}")
        return False
    files = [v for n, v in recs if n == 15]
    if len(files) != 1:
        log(f"FAIL {case['name']}: expected 1 file in the response, got {len(files)}")
        return False
    try:
        sub = list(_cgr_records(files[0]))
    except Exception as exc:
        log(f"FAIL {case['name']}: response file did not parse: {exc}")
        return False
    # A varint where a string belongs parses as None: a malformed response
    # fails this case rather than raising out of the scorer.
    name = next((v for n, v in sub if n == 1), b"")
    content = next((v for n, v in sub if n == 15), b"")
    if not isinstance(name, bytes) or not isinstance(content, bytes):
        log(f"FAIL {case['name']}: response file carries a non-string name or content")
        return False
    name = name.decode("utf-8", "replace")
    if name != case["response_file"]:
        log(f"FAIL {case['name']}: response names {name!r}, expected "
            f"{case['response_file']!r}")
        return False
    want = _blob(case["response_content"], log)
    if content != want:
        log(f"FAIL {case['name']}: {len(content)} bytes of content differ from "
            f"expected {len(want)}")
        return False
    return True


def run_case(protoc, tests_dir, case, log, app_dir="/app"):
    """Run one case. Returns True on pass."""
    workdir = os.path.join(tests_dir, case["cwd"])
    with tempfile.TemporaryDirectory() as tmp:
        give_to_candidate(tmp)
        # {APP} is step 08's addition: a code generator is spawned BY the
        # candidate's protoc via --plugin=NAME=PATH, and /app is deliberately
        # absent from PATH, so the case has to name the candidate's own plugin
        # binary by absolute path.
        def fill(a):
            return (a.replace("{TMP}", tmp)
                     .replace("{TESTS}", tests_dir)
                     .replace("{APP}", app_dir))
        # Before argv: a direct-plugin case has no argv at all -- it runs a
        # BINARY on a recorded request rather than driving the candidate's protoc.
        if case.get("exec"):
            return run_direct_plugin_case(tests_dir, case, log, fill)

        argv = [protoc] + [fill(a) for a in case["argv"]]
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
            err = proc.stderr.decode("utf-8", "replace")
            if not err.strip():
                log(f"FAIL {case['name']}: rejected but stderr was empty")
                return False
            # "Refuses everything" is not the same as refusing THIS. Two ways to
            # show specificity: a case may pin an exact diagnostic token, or -- if
            # it pins none -- the diagnostic must at least name the offending file.
            # Demanding BOTH breaks plugin failures, where the file is not what
            # went wrong: protoc reports `--failing_out: this plugin always fails`
            # and never mentions the .proto at all.
            # Pinned diagnostics are checked HERE, not after the branch. The
            # stderr_contains loop further down belongs to the ordinary path,
            # which a "nonzero" case returns before ever reaching; without this
            # check such a case would reduce to "exited non-zero and said
            # something", which any protoc that rejects an unknown flag satisfies.
            needles = case.get("stderr_contains", [])
            for needle in needles:
                if needle not in err:
                    last = err.strip().splitlines()[-1] if err.strip() else "(empty)"
                    log(f"FAIL {case['name']}: stderr lacks {needle!r}: {last}")
                    return False
            if not needles:
                base = os.path.basename(case["argv"][-1])
                if base not in err:
                    log(f"FAIL {case['name']}: diagnostic does not name {base}")
                    return False
            # ...and refusing everything is not the same as refusing THIS.
            control = case.get("control")
            if control:
                cargv = [protoc] + [
                    fill(a) for a in control
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

        # A plain "exit": 1 cannot tell "protoc drove the plugin and the plugin
        # failed" from "protoc did not recognise the flag at all" -- both exit 1.
        # Without this, a tree with NO plugin support scores these cases, i.e.
        # gets credit for the absence of the capability being measured.
        for needle in case.get("stderr_contains", []):
            err = proc.stderr.decode("utf-8", "replace")
            if needle not in err:
                log(f"FAIL {case['name']}: stderr lacks {needle!r}: "
                    f"{err.strip().splitlines()[-1] if err.strip() else '(empty)'}")
                return False

        if case.get("nonempty_stdout") and not proc.stdout.strip():
            log(f"FAIL {case['name']}: expected non-empty stdout")
            return False

        # A case that generates N files must be checked on all N. Naming one
        # lets a candidate that writes only the last file pass a case whose
        # whole point is that both were written.
        if case.get("outfiles"):
            for spec in case["outfiles"]:
                path = fill(spec["path"])
                if not os.path.exists(path):
                    log(f"FAIL {case['name']}: expected output file {path} not written")
                    return False
                with open(path, "rb") as fh:
                    got = fh.read()
                want = _blob(spec["expect"], log)
                if got != want:
                    log(f"FAIL {case['name']}: {path} has {len(got)} bytes, "
                        f"expected {len(want)}")
                    return False
            return True

        if "expect" not in case:
            return True

        want = _blob(case["expect"], log)
        if want is None:
            return False

        if case.get("compare", "stdout") == "stdout":
            got = proc.stdout
        else:
            out_path = fill(case["outfile"])
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


# Unused at this step. Steps 02-05 score conformance families; step 08 declares
# none (codegen, plugin_protocol, regress are all plain), so the
# `kind == "conformance"` branch below never fires and this function never
# runs here. It is kept so the runner stays one file across the chain.
def run_conformance(app, tests_dir, fam, log):
    """Score one conformance family. Returns (passed, total).

    The runner is an external program and it can fail in ways that are not a
    score: it dies with SIGABRT when a candidate implements half of a
    well-known-type JSON form (jsoncpp's asString() throws on the object it
    gets), producing no summary and no failure list at all. A half-built
    feature is the normal state of a candidate mid-task, so that is the
    expected case, not an edge case. It must cost this family its weight
    without hanging the step or propagating the abort.
    """
    names_path = os.path.join(tests_dir, fam["names"])
    with open(names_path, encoding="utf-8") as fh:
        names = [ln.strip() for ln in fh if ln.strip()]
    mode = fam.get("mode")
    if mode == "binary":
        names = [n for n in names if "ProtobufInput" in n or "ProtoInput" in n]
    elif mode == "json":
        names = [n for n in names if "JsonInput" in n]
    total = len(names)
    if total == 0:
        log(f"family {fam['id']}: 0 names after filtering mode={mode} -- staging bug")
        return 0, 0

    prog = os.path.join(app, "bin", "conformance")
    if not os.path.exists(prog) or not os.access(prog, os.X_OK):
        log(f"family {fam['id']}: {prog} missing or not executable -- 0/{total}")
        return 0, total

    # Unused at this step (no conformance family). If one is ever declared, the
    # testee still runs as the candidate account with its stderr discarded,
    # exactly as in steps 02-05.
    _testee_wrapper = os.path.join(tempfile.mkdtemp(), "testee")
    with open(_testee_wrapper, "w", encoding="utf-8") as _fh:
        _fh.write("#!/bin/sh\nexec %s %s \"$@\" 2>/dev/null\n"
                  % (" ".join(candidate_exec_prefix()), prog))
    os.chmod(_testee_wrapper, 0o755)
    prog = _testee_wrapper
    argv = [RUNNER, "--enforce_recommended"]
    for n in names:
        argv += ["--test", n]
    argv += [prog, SCHEMA_DIR]
    try:
        proc = subprocess.run(argv, stdout=subprocess.PIPE,
                              stderr=subprocess.STDOUT, timeout=CONFORMANCE_TIMEOUT)
    except subprocess.TimeoutExpired:
        log(f"family {fam['id']}: conformance runner exceeded {CONFORMANCE_TIMEOUT}s -- 0/{total}")
        return 0, total
    except OSError as exc:
        log(f"family {fam['id']}: could not run the conformance runner: {exc}")
        return 0, total

    out = proc.stdout.decode("utf-8", "replace")
    if "CONFORMANCE SUITE" not in out:
        tail = [ln for ln in out.strip().splitlines()[-4:]]
        log(f"family {fam['id']}: runner produced NO SUMMARY (exit {proc.returncode}); "
            f"scoring 0/{total}. Last output: {tail}")
        return 0, total

    # The runner prints one summary per suite -- binary/JSON and text-format --
    # so a single match reads one and silently drops the other.
    passed = sum(int(m) for m in re.findall(r"(\d+) successes", out))
    if passed > total:
        log(f"family {fam['id']}: runner reported {passed} successes for {total} "
            f"names; clamping (subset/name mismatch)")
        passed = total
    for ln in out.splitlines():
        if ln.startswith("ERROR, test="):
            log("  " + ln[:200])
    return passed, total


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--app", default="/app")
    ap.add_argument("--tests", default="/tests")
    ap.add_argument("--log", default="/logs/verifier/step08.log")
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
    _n, _rm = _load_expectations(args.tests, spec)
    log(f"expectation guard: {_n} blobs held in memory, {len(_rm)} paths removed from /tests")

    total = 0.0
    T2_FAMILIES = {'regress'}
    _t1, _t2 = [0.0, 0.0], [0.0, 0.0]   # [earned, weight]
    started = time.monotonic()
    log(f"scoring against protoc {spec.get('protoc')} expectations")
    for fam in spec["families"]:
        if fam.get("kind") == "conformance":
            passed, n = run_conformance(args.app, args.tests, fam, log)
            ran = n
        else:
            cases = fam["cases"]
            passed = 0
            ran = 0
            for c in cases:
                if time.monotonic() - started > DEADLINE_SEC:
                    log(f"DEADLINE reached after {ran}/{len(cases)} of family {fam['id']}")
                    break
                ran += 1
                try:
                    _ok = run_case(protoc, args.tests, c, log, args.app)
                except Exception as exc:
                    # Whatever the candidate's programs wrote must cost this one case,
                    # never the step: an uncaught exception would end the scorer.
                    log(f"FAIL {c.get('name')}: scorer could not evaluate this case "
                        f"({type(exc).__name__}: {exc}); the case fails")
                    _ok = False
                if _ok:
                    passed += 1
            n = len(cases)
        # Score against the full set, never against the subset that happened to
        # run: a deadline must cost credit, not silently rescale it.
        frac = passed / n if n else 0.0
        log(f"family {fam['id']}: ran {ran} of {n}")
        contrib = frac * fam["weight"]
        total += contrib
        _fac = _t2 if fam["id"] in T2_FAMILIES else _t1
        _fac[0] += contrib
        _fac[1] += fam["weight"]
        log(f"family {fam['id']:<14} {passed:4d}/{n:<4d} = {frac:.4f}"
            f"  weight {fam['weight']:.2f}  contributes {contrib:.4f}")

    # Product scoring: reward = T1 x T2.  T1 is this step's own
    # milestone (its families' weighted mean, renormalised to [0, 1]); T2 is
    # the regression families over earlier behaviour, renormalised the same
    # way.  A tree that only keeps the old behaviour earns T2 and no T1.
    t1 = _t1[0] / _t1[1] if _t1[1] else 0.0
    t2 = _t2[0] / _t2[1] if _t2[1] else 1.0
    log(f"T1 (this step) {t1:.6f}  x  T2 (regression) {t2:.6f}"
        f"  [weighted sum was {total:.6f}]")
    total = t1 * t2
    total = max(0.0, min(1.0, total))
    log(f"TOTAL reward {total:.6f}")
    print(f"{total:.6f}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
