#!/usr/bin/env python3
"""Step 6 scorer: the subsetter.

The candidate's hb-subset is run over a hidden option grid, and its OUTPUT FONTS
are then judged by the real reference tools that live only in this image:
fontTools structural comparison (the same normalised TTX comparison HarfBuzz's
own subset tests make), OTS 9.3.0 validity, and the real hb-shape.  Earlier
milestones' behaviour is re-checked from fixed samples.
"""
import ctypes
import hashlib, io, json, os, re, shutil, subprocess, time

APP = os.environ.get("SCORE_APP", "/app")
WORK = os.environ.get("SCORE_WORK", "/tmp/graded-app")
TESTS = os.environ.get("SCORE_TESTS", "/tests")
LOGS = os.environ.get("SCORE_LOGS", "/logs/verifier")
OUTDIR = "/tmp/subset-out"
CASE_TIMEOUT, BATCH_TIMEOUT, ROBUST_TIMEOUT = 120, 900, 60
WEIGHTS = {"subset-structure": 0.35, "subset-validity": 0.15, "subset-shaping": 0.10,
           "shaping-regression": 0.10, "vector-regression": 0.10,
           "raster-regression": 0.10, "robustness": 0.10}
# Product scoring: reward = T1 x T2.  T1 is the weighted mean of this
# milestone's own groups, T2 the weighted mean of the groups that re-check
# earlier milestones; WEIGHTS keeps its relative sizes inside each factor.  A
# tree that only keeps earlier behaviour working earns T2 but T1 = 0.
T1_GROUPS = ("subset-structure", "subset-validity", "subset-shaping")
T2_GROUPS = ("shaping-regression", "vector-regression", "raster-regression", "robustness")


def product_reward(scores):
    def wmean(keys):
        return sum(WEIGHTS[k] * scores[k] for k in keys) / sum(WEIGHTS[k] for k in keys)
    t1, t2 = wmean(T1_GROUPS), wmean(T2_GROUPS)
    return t1, t2, t1 * t2


EMPTY_MD5 = "D41D8CD98F00B204E9800998ECF8427E"
GLYPH_LINE = re.compile(r"^ *\d+ ")
TRAILER = re.compile(r"^\s*\d+\s+fails?\.\s*$")
def is_glyph(l):
    """A glyph line, not the "N fails." trailer.

    The trailer starts with digits and matches GLYPH_LINE, so keying
    lines by split()[0] would file it under glyph N and clobber that
    glyph's real line.  It belongs with the informative lines."""
    return bool(GLYPH_LINE.match(l)) and not TRAILER.match(l)

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



def prepare():
    if os.path.exists(WORK):
        shutil.rmtree(WORK)
    shutil.copytree(APP, WORK, symlinks=False, ignore=_ignore_for_copy)
    _seal_workspace()


def env():
    return dict(os.environ, LC_ALL="C", GUARD_ROOT=WORK, NODE_PATH="/nonexistent", HOME="/tmp")


def run(tool, argv, timeout, stdin=None):
    _sweep()
    cmd = ["node", *NODE_FLAGS, "--require", f"{TESTS}/guard.cjs", f"{WORK}/bin/{tool}.js"] + argv
    try:
        p = subprocess.run(cmd, input=stdin, capture_output=True, text=True,
                           timeout=_cand_timeout(timeout), env=env(), cwd="/tmp", **CAND)
        return p.returncode, p.stdout, p.stderr, False
    except subprocess.TimeoutExpired:
        return -1, "", "timeout", True
    except Exception as e:                                    # noqa: BLE001
        return -1, "", f"{type(e).__name__}: {e}", False


def ttx_digests(path):
    from fontTools.ttLib import TTFont
    f = TTFont(path, fontNumber=0, lazy=False)
    out = {}
    for tag in sorted(f.keys()):
        buf = io.StringIO()
        try:
            f.saveXML(buf, tables=[tag])
        except Exception as e:                                # noqa: BLE001
            out[tag] = "ERR:" + type(e).__name__
            continue
        x = buf.getvalue()
        x = re.sub(r'<checkSumAdjustment value="[^"]*"/>', "", x)
        x = re.sub(r'<(created|modified) value="[^"]*"/>', "", x)
        out[tag] = hashlib.sha256(re.sub(r"\s+", " ", x).strip().encode()).hexdigest()[:16]
    order = f.getGlyphOrder()
    try:
        cm = {f"{k:04X}": v for k, v in sorted(f.getBestCmap().items())}
    except Exception:                                         # noqa: BLE001
        cm = {}
    f.close()
    return out, list(order), cm


def score_subset(cases, fontdir):
    # The candidate's uid writes its subset fonts here; root then reads them.
    if os.path.islink(OUTDIR):
        os.unlink(OUTDIR)
    shutil.rmtree(OUTDIR, ignore_errors=True)
    os.makedirs(OUTDIR)
    os.chown(OUTDIR, CAND_UID, CAND_GID)
    os.chmod(OUTDIR, 0o700)
    struct_num = 0.0
    struct_den = float(sum(len(c["table_digests"]) + 2 for c in cases))  # full denominator
    val_ok = 0
    val_tot = len(cases)
    shape_ok = 0
    shape_tot = len(cases)
    misses = []
    for i, c in enumerate(cases):
        if out_of_time():
            break
        src = f"{fontdir}/{c['font']}"
        outp = f"{OUTDIR}/{i}.ttf"
        if os.path.exists(outp):
            os.remove(outp)
        rc, so, se, to = run("hb-subset", [f"--output-file={outp}"] + c["opts"] + [src], CASE_TIMEOUT)
        # No candidate process may outlive the call: root checks and then reads
        # outp, and a leftover one could swap it for a link or a FIFO in between.
        _sweep()
        exp_tables = set(c["table_digests"])
        if os.path.islink(outp) or not os.path.isfile(outp) or os.path.getsize(outp) == 0:
            if len(misses) < 30:
                misses.append(dict(font=c["font"], opts=c["opts"], rc=rc,
                                   timed_out=to, err=(se or "")[:160], produced=False))
            continue
        try:
            dig, order, cm = ttx_digests(outp)
        except Exception as e:                                # noqa: BLE001
            if len(misses) < 30:
                misses.append(dict(font=c["font"], opts=c["opts"], err="ttx: " + str(e)[:120]))
            dig, order, cm = {}, [], {}
        struct_num += sum(1 for t in exp_tables if dig.get(t) == c["table_digests"][t])
        struct_num += 1 if order[:40] == c["glyph_order"] else 0
        struct_num += 1 if cm == c["cmap"] else 0
        # Validity and shaping are credited only for a font that IS this case's
        # subset -- the reference's glyph order and cmap.  An unsubsetted copy of
        # the input passes OTS and shapes like the original, and would otherwise
        # collect a sizeable share of this step's own weight.
        is_subset = order[:40] == c["glyph_order"] and cm == c["cmap"]
        ots = subprocess.run(["ots-sanitize", outp], capture_output=True, text=True, timeout=60)
        if is_subset and (ots.returncode == 0) == c["ots_ok"]:
            val_ok += 1
        # The shaping text is CARRIED by the case, not re-derived from the
        # subset opts.  Both recorders shaped with the original font's first
        # <=6 cmap codepoints regardless of opts, so deriving it from the opts
        # here would make some cases unreachable by ANY implementation, the
        # reference included.
        # stdin is closed: hb-shape with no text argument blocks on an
        # inherited pipe until the timeout (60 s per case).
        shaped = subprocess.run(["hb-shape", outp] + c["shape_opts"],
                                capture_output=True, text=True, timeout=60,
                                stdin=subprocess.DEVNULL)
        if is_subset and shaped.stdout.strip() == c["shaped"]:
            shape_ok += 1
        elif len(misses) < 30 and dig:
            missing = sorted(t for t in exp_tables if dig.get(t) != c["table_digests"][t])
            misses.append(dict(font=c["font"], opts=c["opts"], bad_tables=missing[:8]))
        os.remove(outp)
    return (struct_num / struct_den if struct_den else 0.0,
            val_ok / val_tot if val_tot else 0.0,
            shape_ok / shape_tot if shape_tot else 0.0, misses)


def score_shaping(cases, fontdir):
    lines = [";".join([f"{fontdir}/{c['font']}" if a == "FONT" else a for a in c["argv"]]) for c in cases]
    rc, out, err, to = run("hb-shape", ["--batch"], BATCH_TIMEOUT, stdin="\n".join(lines) + "\n")
    got = out.splitlines()
    if len(got) != len(cases):
        got = []
        for c in cases:
            if out_of_time():
                break
            argv = [f"{fontdir}/{c['font']}" if a == "FONT" else a for a in c["argv"]]
            _r, o, _e, _t = run("hb-shape", argv, 60)
            got.append(o.strip().splitlines()[0] if o.strip() else "")
    ok = sum(1 for c, g in zip(cases, got) if g.strip() == c["expected"].strip())
    return ok, len(cases)


def score_vector(cases, fontdir):
    ok = 0
    for c in cases:
        if out_of_time():
            break
        argv = [f"{fontdir}/{c['font']}" if a == "FONT" else a for a in c["argv"]]
        _rc, out, _e, _t = run("hb-vector", argv, 60)
        if out.strip() == c["expected"].strip():
            ok += 1
    return ok, len(cases)


def score_raster(cases, fontdir):
    matched = 0
    # Full denominator, counted before the loop: every expected non-empty glyph
    # line across the COMPLETE case list.  Counting inside the loop would let an
    # out_of_time() break shrink the denominator and raise the score.
    total = sum(1 for c in cases for l in c["expected"].splitlines()
                if is_glyph(l) and EMPTY_MD5 not in l)
    for c in cases:
        if out_of_time():
            break
        path = f"{fontdir}/{c['font']}"
        argv = [a if a != "FONT" else path for a in c["argv"]]
        _rc, out, _e, _t = run("ftlint", argv, 60)
        out = out.replace(path, "FONT")
        by = {}
        for l in out.splitlines():
            if is_glyph(l):
                by[l.split()[0]] = l
        for l in c["expected"].splitlines():
            if not is_glyph(l) or EMPTY_MD5 in l:
                continue
            if by.get(l.split()[0]) == l:
                matched += 1
    return matched, total


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
        rc, out, err, timed_out = run("ftlint",
                                         ["-f", "2", "-r", "0", "-i", "0-31", "16", path],
                                         ROBUST_TIMEOUT)
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
    out = {"groups": {}, "weights": WEIGHTS}
    os.makedirs(LOGS, exist_ok=True)
    if not os.path.isdir(APP):
        json.dump(dict(reward=0.0, error="no /app"), open(f"{LOGS}/score.json", "w"))
        return
    prepare()
    missing = [t for t in ("ftlint", "hb-shape", "hb-vector", "hb-subset")
               if not os.path.isfile(f"{WORK}/bin/{t}.js")]
    out["missing_tools"] = missing

    st, va, sh, smiss = (0.0, 0.0, 0.0, []) if "hb-subset" in missing else \
        score_subset(interleave(json.load(open(f"{TESTS}/subset-hidden.json"))), f"{TESTS}/corpus/subset-fonts")
    gm, gt = (0, 1) if "hb-shape" in missing else \
        score_shaping(interleave(json.load(open(f"{TESTS}/shaping-regression.json"))), f"{TESTS}/corpus/shape-reg-fonts")
    vm, vt = (0, 1) if "hb-vector" in missing else \
        score_vector(interleave(json.load(open(f"{TESTS}/vector-regression.json"))), f"{TESTS}/corpus/vector-reg-fonts")
    rm, rt = (0, 1) if "ftlint" in missing else \
        score_raster(interleave(json.load(open(f"{TESTS}/raster-regression.json"))), f"{TESTS}/corpus/raster-fonts")
    rob, robd = (0.0, {"skipped": True}) if "ftlint" in missing else \
        score_robustness(interleave(json.load(open(f"{TESTS}/robust.json"))), f"{TESTS}/corpus/robust")

    vals = {"subset-structure": st, "subset-validity": va, "subset-shaping": sh,
            "shaping-regression": gm / gt if gt else 0.0,
            "vector-regression": vm / vt if vt else 0.0,
            "raster-regression": rm / rt if rt else 0.0,
            "robustness": rob}
    t1, t2, reward = product_reward(vals)
    out["T1"], out["T2"] = round(t1, 6), round(t2, 6)
    for k, v in vals.items():
        out["groups"][k] = dict(score=v)
    out["groups"]["robustness"].update(robd)
    out["subset_misses"] = smiss
    out["reward"] = max(0.0, min(1.0, reward))
    out["elapsed_sec"] = round(time.time() - t0, 1)
    json.dump(out, open(f"{LOGS}/score.json", "w"), indent=1)
    for k, v in vals.items():
        print(f"{k:20s} {v:.4f} (weight {WEIGHTS[k]})")
    print(f"T1 {t1:.6f} x T2 {t2:.6f}")
    print(f"reward {out['reward']:.6f} in {out['elapsed_sec']}s")


main()
