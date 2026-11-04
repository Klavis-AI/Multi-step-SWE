#!/usr/bin/env python3
"""Step 1 scorer.

Grades the candidate's `ftlint` against FreeType 2.14.3-precomputed output over a
hidden, stratified font corpus, plus robustness over malformed fonts.

Everything is re-derived here: nothing the candidate writes is trusted, and the
candidate's tool is run from a copy of the workspace with `node_modules` removed,
under a guard that blocks WebAssembly, native addons, child processes, network
and any module outside the workspace.
"""
import ctypes
import json, os, re, shutil, subprocess, sys, time

APP = os.environ.get("SCORE_APP", "/app")
WORK = os.environ.get("SCORE_WORK", "/tmp/graded-app")
TESTS = os.environ.get("SCORE_TESTS", "/tests")
LOGS = os.environ.get("SCORE_LOGS", "/logs/verifier")
CORPUS = f"{TESTS}/corpus"
CASE_TIMEOUT = 60
ROBUST_TIMEOUT = 60

WEIGHTS = {
    "raster-unhinted-glyf": 0.25,
    "raster-cff": 0.25,
    "raster-hinted": 0.35,
    "face-header": 0.05,
    "robustness": 0.10,
}
EMPTY_MD5 = "D41D8CD98F00B204E9800998ECF8427E"
TRAILER = re.compile(r"^\s*\d+\s+fails?\.\s*$")
def is_glyph(l):
    return bool(GLYPH_LINE.match(l)) and not TRAILER.match(l)
GLYPH_LINE = re.compile(r"^ *\d+ ")

# --- A candidate that is slow on one family must not lose the families that
# come after it.  Interleave the scored set across its families at scoring time,
# and give every loop a deadline so partial progress still counts.
DEADLINE = float(os.environ.get("SCORE_DEADLINE", "3000"))
_T0 = time.time()


def out_of_time():
    return (time.time() - _T0) > DEADLINE


# --- OS-level isolation. -----------------------------------------------------
# guard.cjs is an in-process fence and can be bypassed: a file:// URL can read
# the answers past an fs wrapper, and an ES-module `import` of child_process
# (never routed through the CJS hook it patches) can exec the reference tools
# in this image.  So the fences that hold are enforced outside the candidate's
# process: test.sh makes /tests root-only except corpus/ and guard.cjs, and
# shuts the reference tools; every candidate process runs as CAND_UID, never
# root, under Node's permission model (no child processes, workers, addons or
# WASI).  The guard stays as the second line.
CAND_UID = CAND_GID = 65534


def _no_new_privs():
    """Runs in the child after the uid drop: from here on, no exec can gain
    privileges through a set-id bit or a file capability.
    Fails closed: the tool does not run if the kernel refuses."""
    if _LIBC.prctl(38, 1, 0, 0, 0) != 0:          # PR_SET_NO_NEW_PRIVS
        raise OSError(ctypes.get_errno(), "prctl(PR_SET_NO_NEW_PRIVS) failed")


_LIBC = ctypes.CDLL(None, use_errno=True)
CAND = dict(user=CAND_UID, group=CAND_GID, extra_groups=[], preexec_fn=_no_new_privs)
# --jitless: V8 runs without a compiler, so there is no WebAssembly in ANY
# context (a fresh vm context still has one with only the guard's delete).
# --freeze-flags-after-init: v8.setFlagsFromString('--no-jitless') would switch
# it back on; with flags frozen that call aborts the process instead.
NODE_FLAGS = ["--permission", "--allow-fs-read=*", "--allow-fs-write=*", "--no-addons",
              "--jitless", "--freeze-flags-after-init"]
# No candidate call outlives HARD_END; test.sh kills the scorer at 3300 s, and
# Harbor discards a verifier that outlives timeout_sec (3600 s).
HARD_END = DEADLINE + 150


def _cand_timeout(t):
    return max(1.0, min(float(t), HARD_END - (time.time() - _T0)))


def _sweep():
    """SIGKILL every process of CAND_UID, until none is left."""
    for _ in range(50):
        pids = []
        for d in os.listdir("/proc"):
            if not d.isdigit():
                continue
            try:
                with open(f"/proc/{d}/status") as fh:
                    for line in fh:
                        if line.startswith("Uid:"):
                            if str(CAND_UID) in line.split()[1:]:
                                pids.append(int(d))
                            break
            except OSError:
                pass
        if not pids:
            return
        for p in pids:
            try:
                os.kill(p, 9)
            except OSError:
                pass
        time.sleep(0.05)


def _ignore_for_copy(src, names):
    """node_modules, special files, and every symlink that leaves /app: copytree
    runs as root and follows links, so a link to /tests/<answers>.json would be
    copied in as a file the candidate's uid can read."""
    app = os.path.realpath(APP)
    skip = set()
    for n in names:
        p = os.path.join(src, n)
        if n == "node_modules":
            skip.add(n)
        elif os.path.islink(p):
            r = os.path.realpath(p)
            if not (r == app or r.startswith(app + os.sep)) or not (
                    os.path.isfile(r) or os.path.isdir(r)):
                skip.add(n)
        elif not (os.path.isfile(p) or os.path.isdir(p)):
            skip.add(n)
    return skip


def _seal_workspace():
    """The copy is the candidate's code: readable by CAND_UID, writable by root only."""
    for root, dirs, files in os.walk(WORK):
        os.chown(root, 0, 0)
        os.chmod(root, 0o755)
        for f in files:
            p = os.path.join(root, f)
            st = os.lstat(p)
            os.chown(p, 0, 0)
            os.chmod(p, 0o755 if st.st_mode & 0o111 else 0o644)


def interleave(cases, key="file"):
    groups = {}
    for c in cases:
        groups.setdefault(c.get(key) or c.get("font") or "", []).append(c)
    out, i = [], 0
    while True:
        added = False
        for k in sorted(groups):
            if i < len(groups[k]):
                out.append(groups[k][i]); added = True
        if not added:
            return out
        i += 1



def prepare_workspace():
    """Copy /app aside and strip anything that could smuggle in an engine."""
    if os.path.exists(WORK):
        shutil.rmtree(WORK)
    shutil.copytree(APP, WORK, symlinks=False, ignore=_ignore_for_copy)
    _seal_workspace()
    findings = []
    for root, dirs, files in os.walk(WORK):
        dirs[:] = [d for d in dirs if d != "node_modules"]
        for f in files:
            if f.endswith((".wasm", ".node")):
                findings.append(os.path.relpath(os.path.join(root, f), WORK))
    return findings


def run_tool(argv, timeout):
    _sweep()
    cmd = ["node", *NODE_FLAGS, "--require", f"{TESTS}/guard.cjs",
           f"{WORK}/bin/ftlint.js"] + argv
    env = dict(os.environ, LC_ALL="C", GUARD_ROOT=WORK, NODE_PATH="/nonexistent", HOME="/tmp")
    try:
        p = subprocess.run(cmd, capture_output=True, text=True, timeout=_cand_timeout(timeout),
                           env=env, cwd="/tmp", **CAND)
        return p.returncode, p.stdout, p.stderr, False
    except subprocess.TimeoutExpired:
        return -1, "", "timeout", True
    except Exception as e:                      # noqa: BLE001
        return -1, "", f"{type(e).__name__}: {e}", False


def glyph_lines(text):
    return [l for l in text.splitlines() if is_glyph(l)]


def other_lines(text):
    """Non-glyph lines that actually carry information: the
    `family style, N ppem, FLAGS, mode` line and the trailer.  The file line
    merely echoes the argument, and the column header and its rule are constant
    text, so none of those three is compared -- see the skips below, and
    workdir/INTERFACE.md section 4, which states the same three exclusions."""
    out = []
    for l in text.splitlines():
        if is_glyph(l) or not l.strip():
            continue
        if l.lstrip().startswith("GID") or set(l.strip()) == {"-"}:
            continue
        if l.rstrip().endswith(":"):
            continue          # the file line merely echoes the argument
        out.append(l)
    return out


def score_raster(cases, fontdir):
    """Per group: matched non-empty glyph lines / expected non-empty glyph lines."""
    groups = {k: [0, 0] for k in ("raster-unhinted-glyf", "raster-cff", "raster-hinted")}
    header = [0, 0]
    detail = []
    # Denominators cover the COMPLETE scored set, not merely the cases a run had
    # time to reach: otherwise a run cut short by the deadline scores full marks
    # on whatever it managed first.  Only numerators accumulate inside the loop.
    for c in cases:
        gk = ("raster-hinted" if c["flags"] == "8000"
              else "raster-cff" if c["stratum"] == "cff" else "raster-unhinted-glyf")
        groups[gk][1] += sum(1 for l in c["expected"].splitlines()
                             if is_glyph(l) and EMPTY_MD5 not in l)
        header[1] += 1
    for c in cases:
        if out_of_time():
            break
        path = os.path.join(fontdir, c["font"])
        argv = [a if a != "FONT" else path for a in c["argv"]]
        rc, out, err, timed_out = run_tool(argv, CASE_TIMEOUT)
        out = out.replace(path, "FONT")
        exp = c["expected"]
        e_g, g_g = glyph_lines(exp), glyph_lines(out)
        e_o, g_o = other_lines(exp), other_lines(out)

        by_gid = {}
        for l in g_g:
            by_gid[l.split()[0]] = l
        matched = 0
        total = 0
        for l in e_g:
            if EMPTY_MD5 in l:
                continue                       # an empty bitmap is not evidence
            total += 1
            if by_gid.get(l.split()[0]) == l:
                matched += 1
        if c["flags"] == "8000":
            gk = "raster-hinted"
        elif c["stratum"] == "cff":
            gk = "raster-cff"
        else:
            gk = "raster-unhinted-glyf"
        groups[gk][0] += matched
        # The denominator is counted over the complete case list in the pre-pass;
        # only the numerator belongs here.
        header[0] += 1 if e_o == g_o else 0
        detail.append(dict(font=c["font"], ppem=c["ppem"], flags=c["flags"],
                           stratum=c["stratum"], matched=matched, total=total,
                           rc=rc, timed_out=timed_out,
                           err=(err or "")[:200] if matched < total else ""))
    return groups, header, detail


def score_robustness(manifest, fontdir):
    """Malformed and fuzzed fonts.

    Liveness is not credit: a font counts only when the engine (a) does not
    crash or hang, (b) reaches the same load / do-not-load verdict FreeType
    reaches, and (c) where FreeType loads the font, reproduces its non-empty
    glyph lines exactly.  A stub that prints plausible lines fails (b) and (c);
    a tool that always crashes fails (a)."""
    # Fonts that FreeType loads but renders entirely empty cannot distinguish a
    # real engine from a stub that prints "void / 0x0" lines, so they are not
    # evidence and are excluded from the denominator.  What remains discriminates:
    # fonts with real glyph output, and fonts FreeType refuses to load at all.
    manifest = [m for m in manifest
                if (not m["ft_loads"]) or m.get("ft_nonempty_lines", 1) > 0]
    n = len(manifest)
    ok = 0
    clean = 0
    agreed = 0
    bad = []
    ran = 0
    for m in manifest:
        if out_of_time():
            break
        ran += 1
        path = f"{fontdir}/{m['file']}"
        rc, out, err, timed_out = run_tool(
            ["-f", "2", "-r", "0", "-i", "0-31", "16", path], ROBUST_TIMEOUT)
        # The real ftlint always exits 0, even when a face cannot be opened and
        # even when glyphs fail: it reports failures in its output.  So a
        # non-zero exit is the engine failing to run, not a verdict — otherwise a
        # tool that throws on every font would be credited with "correctly
        # rejecting" the fonts FreeType also rejects.
        # Node 22 compiles its bundled undici's llhttp blob during teardown; the
        # guard has already removed WebAssembly, so a tool can exit non-zero
        # AFTER producing correct output.  That is a runtime artefact, not the
        # engine failing to run, and crediting it as a crash would forfeit this
        # whole group for a reason that has nothing to do with robustness.
        # Matched by signature so a genuine candidate crash is still caught.
        _undici_teardown = ("node:internal/deps/undici" in (err or "")
                            and "WebAssembly" in (err or ""))
        crashed = (timed_out or (rc != 0 and not _undici_teardown)
                   or ("uncaughtException" in (err or "") and not _undici_teardown)
                   or ("Error:" in (err or "") and not _undici_teardown))
        if not crashed:
            clean += 1
        out = out.replace(path, "FONT")
        got = {}
        for l in out.splitlines():
            if is_glyph(l):
                got[l.split()[0]] = l
        we_load = bool(got)
        verdict = (we_load == bool(m["ft_loads"]))
        if verdict:
            agreed += 1
        # Every font, loadable or not, must also reproduce FreeType's informative
        # non-glyph lines (the family/ppem/flags line and trailer, or the opening
        # error).  Without this a tool that prints nothing and exits 0 is credited
        # with "correctly rejecting" the fonts FreeType also rejects.
        want_info = [l for l in m["ft_output"].splitlines()
                     if l.strip() and not is_glyph(l)
                     and not l.lstrip().startswith("GID")
                     and set(l.strip()) != {"-"} and not l.rstrip().endswith(":")]
        got_info = [l for l in out.splitlines()
                    if l.strip() and not is_glyph(l)
                    and not l.lstrip().startswith("GID")
                    and set(l.strip()) != {"-"} and not l.rstrip().endswith(":")]
        lines_ok = (want_info == got_info)
        if lines_ok and m["ft_loads"]:
            for l in m["ft_output"].splitlines():
                if not is_glyph(l) or EMPTY_MD5 in l:
                    continue
                if got.get(l.split()[0]) != l:
                    lines_ok = False
                    break
        if (not crashed) and verdict and lines_ok:
            ok += 1
        elif len(bad) < 20:
            bad.append(dict(file=m["file"], crashed=crashed, verdict_ok=verdict,
                            lines_ok=lines_ok, err=(err or "")[:120]))
    return (ok / n if n else 0.0), dict(fonts=n, ran=ran, scored_ok=ok,
                                        no_crash=clean, verdict_agreed=agreed,
                                        failures=bad)

def main():
    t0 = time.time()
    result = dict(groups={}, weights=WEIGHTS)
    if not os.path.isdir(APP):
        json.dump(dict(reward=0.0, error="no /app"), open(f"{LOGS}/score.json", "w"))
        print("no /app"); return
    findings = prepare_workspace()
    result["binary_artifacts_in_workspace"] = findings
    if not os.path.isfile(f"{WORK}/bin/ftlint.js"):
        json.dump(dict(reward=0.0, error="missing /app/bin/ftlint.js", groups={}),
                  open(f"{LOGS}/score.json", "w"))
        print("missing /app/bin/ftlint.js -> reward 0")
        return

    cases = interleave(json.load(open(f"{TESTS}/hidden-ftlint.json")))
    groups, header, detail = score_raster(cases, f"{CORPUS}/hidden")
    robust_manifest = interleave(json.load(open(f"{TESTS}/robust.json")))
    robust_score, robust_detail = score_robustness(robust_manifest, f"{CORPUS}/robust")

    reward = 0.0
    for k, (m, t) in groups.items():
        s = (m / t) if t else 0.0
        result["groups"][k] = dict(score=s, matched=m, total=t)
        reward += WEIGHTS[k] * s
    hs = (header[0] / header[1]) if header[1] else 0.0
    result["groups"]["face-header"] = dict(score=hs, matched=header[0], total=header[1])
    reward += WEIGHTS["face-header"] * hs
    result["groups"]["robustness"] = dict(score=robust_score, **robust_detail)
    reward += WEIGHTS["robustness"] * robust_score

    result["reward"] = max(0.0, min(1.0, reward))
    result["elapsed_sec"] = round(time.time() - t0, 1)
    json.dump(result, open(f"{LOGS}/score.json", "w"), indent=1)
    json.dump(detail, open(f"{LOGS}/detail.json", "w"))
    for k, v in result["groups"].items():
        print(f"{k:22s} {v['score']:.4f}  (weight {WEIGHTS[k]})")
    print(f"reward {result['reward']:.6f} in {result['elapsed_sec']}s")


main()
