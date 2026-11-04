#!/usr/bin/env python3
"""Run the hidden conformance corpus against the candidate interpreter.

Each test is one Lua program run as `lua <argv...>` from a private directory with
the interpreter first on PATH, so diagnostics always name the program `lua`.
A test passes when stdout, the process exit status and the first line of stderr
all match what the reference implementation produced at authoring time.
"""
import argparse, json, os, re, shutil, signal, subprocess, sys, tempfile, threading, time
from concurrent.futures import ThreadPoolExecutor

# A scored program gets a wall-clock limit and a resident-memory ceiling. Both
# are stated in the milestone instruction: a program that exceeds either is a
# failed check, never a wedged or evicted grader.
MEM_LIMIT_BYTES = 1024 * 1024 * 1024

# Scored programs run here, not under /tmp, so the directory can be made
# writable by the unprivileged uid without opening anything else.
RUN_DIR = "/run-corpus"



def tree_rss(pid):
    """Resident bytes of the process and everything it started."""
    total = 0
    seen = []
    try:
        stack = [int(pid)]
        while stack:
            q = stack.pop()
            if q in seen:
                continue
            seen.append(q)
            try:
                with open("/proc/%d/statm" % q) as f:
                    total += int(f.read().split()[1]) * os.sysconf("SC_PAGE_SIZE")
                with open("/proc/%d/task/%d/children" % (q, q)) as f:
                    stack.extend(int(x) for x in f.read().split())
            except (OSError, ValueError, IndexError):
                continue
    except (OSError, ValueError):
        pass
    return total


# The graded interpreter runs as this uid so that it cannot read the corpus or
# the recorded expectations, which are root-only.
RUN_UID = int(os.environ.get("LUA_GRADER_UID", "4242"))
RUN_GID = RUN_UID


def drop_privs():
    # A preexec_fn: runs in the forked child before exec. It must not swallow a
    # failure -- a drop that quietly does nothing would run candidate code as
    # root, exactly the case it exists to prevent. So it raises on
    # failure (the Popen then fails and the program is scored as not-run) and
    # verifies the post-condition rather than trusting the calls.
    os.setgroups([])
    os.setgid(RUN_GID)
    os.setuid(RUN_UID)
    if os.getuid() != RUN_UID or os.geteuid() != RUN_UID :
        os._exit(71)


def run_limited(argv, cwd, env, timeout):
    """Run argv in its own process group under a time and memory ceiling.

    Returns (stdout, stderr, status) or raises subprocess.TimeoutExpired. On
    either limit the whole group is killed, so a runaway child cannot outlive
    the check that started it.
    """
    pre = drop_privs if os.geteuid() == 0 else None
    p = subprocess.Popen(argv, cwd=cwd, env=env,
                         stdin=subprocess.DEVNULL,
                         stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                         start_new_session=True, preexec_fn=pre)
    over = []

    def watch():
        while p.poll() is None:
            if tree_rss(p.pid) > MEM_LIMIT_BYTES:
                over.append(True)
                try:
                    os.killpg(p.pid, signal.SIGKILL)
                except OSError:
                    pass
                return
            time.sleep(0.25)

    w = threading.Thread(target=watch, daemon=True)
    w.start()
    try:
        out, err = p.communicate(timeout=timeout)
    except subprocess.TimeoutExpired:
        try:
            os.killpg(p.pid, signal.SIGKILL)
        except OSError:
            pass
        p.communicate()
        raise
    if over:
        return None, None, None
    return out, err, p.returncode


_GPREFIX = re.compile(r"to '_G" + "\\" + ".")

def normalize(text):
    """Drop the '_G.' qualifier from argument-error messages.

    From 5.2 on, a library function whose call site has no name is reported by
    the name luaL_argerror finds for it in package.loaded, and which name that
    search reaches first depends on the per-process string hash seed. Both
    spellings are the reference implementation's own output, so grading accepts
    either.
    """
    return _GPREFIX.sub("to '", text)

def run_one(bindir, corpus, name, timeout):
    d = tempfile.mkdtemp(prefix="conf.", dir=RUN_DIR)
    try:
        # The program's own directory belongs to the uid it runs as, and to
        # nobody else: one graded program cannot reach into another's.
        if os.geteuid() == 0:
            os.chown(d, RUN_UID, RUN_GID)
        os.chmod(d, 0o700)
        # Each program and its auxiliary files are copied in with permissions
        # of their own: the originals under /tests stay unreadable to the uid
        # the interpreter runs as.
        def place(src, dst):
            shutil.copy(src, dst)
            os.chmod(dst, 0o644)

        place(os.path.join(corpus, name), os.path.join(d, "t.lua"))
        aux_dir = os.path.join(corpus, name[:-4] + ".aux")
        if os.path.isdir(aux_dir):
            for f in sorted(os.listdir(aux_dir)):
                place(os.path.join(aux_dir, f), os.path.join(d, f))
        argv_file = os.path.join(corpus, name[:-4] + ".argv")
        if os.path.exists(argv_file):
            argv = [l for l in open(argv_file).read().split("\n") if l != ""]
        else:
            argv = ["t.lua"]
        env = dict(os.environ)
        env["PATH"] = bindir + os.pathsep + env.get("PATH", "")
        env["LC_ALL"] = "C"
        env["LANG"] = "C"
        for k in ("LUA_INIT", "LUA_PATH", "LUA_CPATH"):
            env.pop(k, None)
        env_file = os.path.join(corpus, name[:-4] + ".env")
        if os.path.exists(env_file):
            for line in open(env_file).read().split("\n"):
                if "=" in line:
                    k, v = line.split("=", 1)
                    env[k] = v
        try:
            out, err, rc = run_limited(["lua"] + argv, d, env, timeout)
            if out is None:
                return {"stdout": "<over memory limit>", "status": -1,
                        "err1": "<over memory limit>"}
        except subprocess.TimeoutExpired:
            return {"stdout": "<timeout>", "status": -1, "err1": "<timeout>"}
        except OSError as e:
            return {"stdout": "<oserror>", "status": -1, "err1": str(e)}
        err1 = err.split(b"\n", 1)[0].decode("latin-1") if err else ""
        return {"stdout": normalize(out.decode("latin-1")), "status": rc,
                "err1": normalize(err1)}
    finally:
        shutil.rmtree(d, ignore_errors=True)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--app", required=True)
    ap.add_argument("--corpus", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--build", default="default",
                    help="run only the programs recorded for this build")
    ap.add_argument("--jobs", type=int, default=7)
    ap.add_argument("--timeout", type=float, default=25)
    ap.add_argument("--deadline-epoch", type=float, default=0.0,
                    help="stop starting programs at this time; report what ran")
    a = ap.parse_args()

    bindir = os.path.abspath(a.app)      # the workspace itself goes on PATH, so
                                         # the launcher sees its own directory
    with open(os.path.join(a.corpus, "expected.json")) as f:
        exp = json.load(f)
    # The liveness canaries run first (so the gate still means something under a
    # deadline), then the rest interleaved across their groups so a deadline hit
    # starves no single family -- alphabetical order would run whole families
    # last and drop them together.
    sel = [n for n in exp if exp[n].get("build", "default") == a.build]
    canary = sorted(n for n in sel if n.startswith("d_canary_"))
    rest = [n for n in sel if not n.startswith("d_canary_")]
    bygroup = {}
    for n in sorted(rest):
        bygroup.setdefault(exp[n].get("group", "?"), []).append(n)
    order, gs = [], sorted(bygroup)
    while any(bygroup.values()):
        for g in gs:
            if bygroup[g]:
                order.append(bygroup[g].pop(0))
    names = canary + order

    results = {}
    not_run = []
    def work(name):
        if a.deadline_epoch and time.time() > a.deadline_epoch:
            not_run.append(name)
            return name, {"stdout": "<not run: grader deadline>", "status": -1,
                          "err1": "<not run: grader deadline>"}
        return name, run_one(bindir, a.corpus, name, a.timeout)
    with ThreadPoolExecutor(max_workers=a.jobs) as ex:
        for name, got in ex.map(work, names):
            e = exp[name]
            ok = (got["stdout"] == e["stdout"] and got["status"] == e["status"]
                  and got["err1"] == e["err1"])
            # got_status is recorded for every program (not only failures) so
            # the liveness gate can ask whether a canary failed at all,
            # independently of whether its message matched exactly.
            results[name] = {"pass": ok, "group": e.get("group", "?"),
                             "build": a.build, "got_status": got["status"],
                             "want_status": e["status"]}
            if not ok:
                results[name]["got_err1"] = got["err1"][:200]
                results[name]["want_err1"] = e["err1"][:200]
                eo = e["stdout"].split("\n")
                go = got["stdout"].split("\n")
                for i in range(max(len(eo), len(go))):
                    x = eo[i] if i < len(eo) else "<missing>"
                    y = go[i] if i < len(go) else "<missing>"
                    if x != y:
                        results[name]["first_diff_line"] = i + 1
                        results[name]["want"] = x[:200]
                        results[name]["got"] = y[:200]
                        break

    passed = sum(1 for r in results.values() if r["pass"])
    with open(a.out, "w") as f:
        json.dump({"build": a.build, "tests": results,
                   "deadline_hit": bool(not_run), "not_run": len(not_run)},
                  f, indent=1, sort_keys=True)
    print("build %-10s %d/%d programs match" % (a.build, passed, len(results)))
    if not_run:
        print("DEADLINE: %d of %d programs were not started"
              % (len(not_run), len(results)))

main()
