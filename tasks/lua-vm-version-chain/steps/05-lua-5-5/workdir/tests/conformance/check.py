#!/usr/bin/env python3
"""Compare ./lua against the recorded reference output for the example corpus."""
import json, os, re, shutil, signal, subprocess, sys, tempfile, threading, time

# The ceilings every conformance program runs under: 15 s of wall clock and
# 1 GiB of resident memory per program, the whole process group killed on either.
TIME_LIMIT = 15
MEM_LIMIT_BYTES = 1024 * 1024 * 1024


def tree_rss(pid):
    total, seen, stack = 0, [], [int(pid)]
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
    return total


_GPREFIX = re.compile(r"to '_G" + "\\" + ".")

def normalize(text):
    """Drop the '_G.' qualifier from argument-error messages.

    From 5.2 on, a library function whose call site has no name is reported by
    the name luaL_argerror finds for it in package.loaded, and which name that
    search reaches first depends on the per-process string hash seed. Both
    spellings are the reference implementation's own output, so either one
    counts as a match.
    """
    return _GPREFIX.sub("to '", text)

app, corpus = sys.argv[1], sys.argv[2]
only = sys.argv[3] if len(sys.argv) > 3 else None
exp = json.load(open(os.path.join(corpus, "expected.json")))
bindir = os.path.abspath(app)
fails = []
for name in sorted(exp):
    if only and only not in name:
        continue
    d = tempfile.mkdtemp(prefix="conf.")
    shutil.copy(os.path.join(corpus, name), os.path.join(d, "t.lua"))
    aux = os.path.join(corpus, name[:-4] + ".aux")
    if os.path.isdir(aux):
        for f in sorted(os.listdir(aux)):
            shutil.copy(os.path.join(aux, f), os.path.join(d, f))
    af = os.path.join(corpus, name[:-4] + ".argv")
    argv = [l for l in open(af).read().split("\n") if l] if os.path.exists(af) else ["t.lua"]
    env = dict(os.environ)
    env["PATH"] = bindir + os.pathsep + env.get("PATH", "")
    env["LC_ALL"] = env["LANG"] = "C"
    for k in ("LUA_INIT", "LUA_PATH", "LUA_CPATH"):
        env.pop(k, None)
    ef = os.path.join(corpus, name[:-4] + ".env")
    if os.path.exists(ef):
        for line in open(ef).read().split("\n"):
            if "=" in line:
                k, v = line.split("=", 1)
                env[k] = v
    p = subprocess.Popen(["lua"] + argv, cwd=d, env=env, stdout=subprocess.PIPE,
                         stdin=subprocess.DEVNULL, stderr=subprocess.PIPE,
                         start_new_session=True)
    over = []

    def watch(proc=p, flag=over):
        while proc.poll() is None:
            if tree_rss(proc.pid) > MEM_LIMIT_BYTES:
                flag.append(True)
                try:
                    os.killpg(proc.pid, signal.SIGKILL)
                except OSError:
                    pass
                return
            time.sleep(0.25)

    threading.Thread(target=watch, daemon=True).start()
    try:
        out, err = p.communicate(timeout=TIME_LIMIT)
        if over:
            got = {"stdout": "<over memory limit>", "status": -1,
                   "err1": "<over memory limit>"}
        else:
            got = {"stdout": normalize(out.decode("latin-1")), "status": p.returncode,
                   "err1": normalize(err.split(b"\n", 1)[0].decode("latin-1") if err else "")}
    except subprocess.TimeoutExpired:
        try:
            os.killpg(p.pid, signal.SIGKILL)
        except OSError:
            pass
        p.communicate()
        got = {"stdout": "<timeout>", "status": -1, "err1": "<timeout>"}
    shutil.rmtree(d, ignore_errors=True)
    e = exp[name]
    if (got["stdout"], got["status"], got["err1"]) != (e["stdout"], e["status"], e["err1"]):
        fails.append((name, got, e))
total = len([n for n in exp if not only or only in n])
print("%d/%d conformance programs match" % (total - len(fails), total))
for name, got, e in fails[:8]:
    print("=" * 60)
    print("FAIL", name)
    if got["status"] != e["status"]:
        print("  exit status: want %s got %s" % (e["status"], got["status"]))
    if got["err1"] != e["err1"]:
        print("  stderr[0]: want %r" % e["err1"])
        print("             got %r" % got["err1"])
    if got["stdout"] != e["stdout"]:
        eo, go = e["stdout"].split("\n"), got["stdout"].split("\n")
        for i in range(max(len(eo), len(go))):
            a = eo[i] if i < len(eo) else "<missing>"
            b = go[i] if i < len(go) else "<missing>"
            if a != b:
                print("  stdout line %d: want %r" % (i + 1, a))
                print("                 got  %r" % b)
                break
if fails:
    print("\n(%d more)" % max(0, len(fails) - 8) if len(fails) > 8 else "")
