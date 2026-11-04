#!/usr/bin/env python3
"""Scoring runner for tasks/dwarf-debugger-version-chain.

Rebuilds the candidate from the text files in the collected workspace, runs its
four front ends over hidden fixtures, and compares their output to output
recorded from the real tools at authoring time.

The step score is T1 x T2 (see main): T1 is the weighted mean of
matched / total units over this step's own groups, T2 the same fraction for the
inherited "regress" group.  reward.json records 1.0 when that score is 1.0 and
0.0 otherwise.  A tree that does not build scores 0.  Always writes reward.json
and exits 0.
"""
import json
import os
import re
import shutil
import stat
import subprocess
import tempfile
import time

APP = "/app"
WORK = "/work"
TESTS = "/tests"
LOGDIR = "/logs/verifier"
FXROOT = "/dw/out"
MAX_TEXT = 2 * 1024 * 1024
CASE_TIMEOUT = int(os.environ.get("CASE_TIMEOUT", "120"))
# Absolute so the POISON shim below catches only the candidate, never us.
GDB_CLIENT = os.environ.get("GDB_CLIENT", "/usr/bin/gdb")
SCORE_DEADLINE = float(os.environ.get("SCORE_DEADLINE", "2400"))
_started = time.time()


def out_of_time():
    return (time.time() - _started) > SCORE_DEADLINE
BUILD_TIMEOUT = int(os.environ.get("BUILD_TIMEOUT", "900"))

POISON = ["llvm-symbolizer", "llvm-addr2line", "llvm-dwarfdump", "llvm-symbolizer-19",
          "addr2line", "readelf", "objdump", "nm", "eu-stack", "eu-readelf",
          "eu-addr2line", "gdbserver", "gdb", "dwarfdump", "strings", "size", "c++filt",
          "llvm-cxxfilt"]

report = []

# Everything the candidate controls -- its build and its programs -- runs as an
# unprivileged uid, so it cannot reach /logs/verifier even if the directory were
# writable, and cannot touch the staged expectations.
SCORE_UID = 65534


def _root_owned_path(dirs=("/usr/local/sbin", "/usr/local/bin", "/usr/sbin", "/usr/bin",
                           "/sbin", "/bin")):
    """PATH for what root itself runs: system directories that are root's and that nobody
    else can write. The candidate's case PATH (poison shims first, see make_poison) is never
    what root resolves a program through."""
    keep = []
    for d in dirs:
        try:
            st = os.stat(d)
        except OSError:
            continue
        if stat.S_ISDIR(st.st_mode) and st.st_uid == 0 and not st.st_mode & 0o022:
            keep.append(d)
    return ":".join(keep)


ROOT_PATH = _root_owned_path()
# The environment of every process root runs itself: the drop wrapper below and the gdb client.
ROOT_ENV = dict(os.environ, PATH=ROOT_PATH, LC_ALL="C", TZ="UTC")
# Candidate code MUST run unprivileged. Running it as root would let it reach /logs/verifier and
# the staged expectations, so a missing setpriv is a hard refusal, not a warning that scores on.
# Both helpers are resolved once, through ROOT_PATH, and DROP execs setpriv by that absolute
# path, so root never looks a program up on a PATH the candidate could write to.
SETPRIV = shutil.which("setpriv", path=ROOT_PATH)
ENV_BIN = shutil.which("env", path=ROOT_PATH)
DROP = [SETPRIV, "--reuid=%d" % SCORE_UID, "--regid=%d" % SCORE_UID, "--clear-groups"] if SETPRIV else []


def dropped(argv, env):
    """argv run as SCORE_UID with exactly `env` as its environment. Pass env=ROOT_ENV to the
    subprocess call: root runs only setpriv, by absolute path, in ROOT_ENV. The candidate's env
    takes effect after the drop, applied by env(1) running as SCORE_UID, which then execs argv."""
    return DROP + [ENV_BIN, "-i"] + ["%s=%s" % kv for kv in env.items()] + list(argv)


def log(msg):
    report.append(msg)
    print(msg, flush=True)


def write_reward(value, note=""):
    os.makedirs(LOGDIR, exist_ok=True)
    v = max(0.0, min(1.0, float(value)))
    v = 1.0 if v >= 1.0 else 0.0
    with open(os.path.join(LOGDIR, "reward.json"), "w") as fh:
        json.dump({"reward": round(v, 6)}, fh)
    with open(os.path.join(LOGDIR, "report.txt"), "w") as fh:
        fh.write("\n".join(report) + "\n" + note + "\n")
    print("REWARD %.6f %s" % (v, note), flush=True)


def is_text(path):
    try:
        if os.path.getsize(path) > MAX_TEXT:
            return False
        with open(path, "rb") as fh:
            chunk = fh.read(65536)
        if b"\0" in chunk:
            return False
        chunk.decode("utf-8")
        return True
    except Exception:
        return False


def stage_sources():
    """Copy only the candidate's text sources into a clean tree."""
    # Only the top level of the workspace is filtered: these are the directories
    # this task supplies or the build owns.  A candidate directory of the same
    # name deeper in the tree is its own source and must be carried across.
    top_skip = {"build", "bin", "tests-public", ".git", "spec", "__pycache__"}
    n = 0
    for root, dirs, files in os.walk(APP):
        rel = os.path.relpath(root, APP)
        if rel == ".":
            dirs[:] = [d for d in dirs if d not in top_skip]
        else:
            first = rel.split(os.sep)[0]
            if first in top_skip:
                dirs[:] = []
                continue
        for f in files:
            src = os.path.join(root, f)
            if os.path.islink(src) or not is_text(src):
                continue
            dst = os.path.join(WORK, os.path.relpath(src, APP))
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            shutil.copy2(src, dst)
            n += 1
    return n


def make_poison():
    # The shim directory heads the candidate's PATH. It is a fresh root-owned directory outside
    # WORK, so the candidate uid can neither add a program to it nor put a directory of its own
    # in its place, and root never writes into a directory that uid controls. Only the tattle
    # log is in WORK: the shims run as that uid and append to it.
    d = tempfile.mkdtemp(prefix="poison.")
    os.chmod(d, 0o755)
    tattle = os.path.join(WORK, "poison.log")
    for name in POISON:
        fd = os.open(os.path.join(d, name),
                     os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW, 0o755)
        os.fchmod(fd, 0o755)
        with os.fdopen(fd, "w") as fh:
            fh.write("#!/bin/sh\necho \"$0 $*\" >> %s\nexit 127\n" % tattle)
    return d, tattle


FORBIDDEN_SRC = (".c", ".cc", ".cpp", ".cxx", ".m", ".rs", ".go", ".zig",
                 ".s", ".S", ".asm", ".jar", ".so")
FORBIDDEN_CALL = re.compile(
    # Anchored on a COMMAND position, not on a bare word, and tolerant of a
    # version suffix:
    #   - `=` is not a terminator, so `cc` inside `cc=1` (a workspace shell
    #     variable named `cc`) does not match;
    #   - the optional suffix matches versioned binaries (`gcc-14`, `clang-19`,
    #     `clang++-19`, `pip3.11`);
    #   - `)` is not a terminator, so `(g++)` inside an echoed string does not
    #     match.
    # The suffix alternates disjoint classes ([-.] then [\w+]), so it cannot
    # backtrack super-linearly on a long run of dots.
    r"(?:^|[\t;&|(]\s*)(gcc|g\+\+|cc|clang|clang\+\+|rustc|cargo|zig|ghc|"
    r"curl|wget|apt-get|aptitude|pip3?|mvn|gradle|sbt)"
    r"(?:[-.][\w+]+)*(?=$|[\s;&|<>])")
# The variant for make-family files ONLY. A make recipe
# line must begin with a TAB, and may carry the recipe prefixes `@` (do not
# echo), `-` (ignore errors) and `+` (run even under -n) between that tab and
# the command. Extending only the tab branch to skip those prefixes catches
# `\t@gcc` and `\t-gcc` while adding no false-positive surface: every line
# this form matches and the form above does not begins with a tab followed by
# recipe-prefix characters, which is syntax make defines and prose does not.
# `.sh` files keep the form above, because widening there would mean `^\s*`,
# and a false positive costs a correct submission the WHOLE milestone while a
# miss only thins defence in depth (every executable under bin/ is separately
# required to be a JDK launcher). Space-indented commands stay unmatched.
FORBIDDEN_CALL_MAKE = re.compile(
    r"(?:^|[\t][@+-]*\s*|[;&|(]\s*)(gcc|g\+\+|cc|clang|clang\+\+|rustc|"
    r"cargo|zig|ghc|curl|wget|apt-get|aptitude|pip3?|mvn|gradle|sbt)"
    r"(?:[-.][\w+]+)*(?=$|[\s;&|<>])")
FORBIDDEN_NATIVE = re.compile(r"System\.load(?:Library)?\s*\(|libraryLookup\s*\(")
# Spawning another process is the absolute-path escape the POISON PATH shim cannot see: it
# intercepts only bare-name PATH lookups, so a Java source that reaches a real tool through
# Runtime.exec/ProcessBuilder would slip past it. The reference toolkit drives its inferior
# through the FFM API (SymbolLookup.defaultLookup -> ptrace) and never spawns a child, so this
# never fires on a faithful implementation.
FORBIDDEN_EXEC = re.compile(
    r"Runtime\s*\.\s*getRuntime\s*\(\s*\)\s*\.\s*exec\b"
    r"|\bRuntime\s*\.\s*exec\b"
    r"|\bnew\s+ProcessBuilder\b|\bProcessBuilder\s*\(|java\.lang\.ProcessBuilder"
    r"|\bProcessImpl\b")
# Both Java scans read the code, not the comments: INTERFACE section 2
# charges a file that CALLS these APIs, and a comment that merely names one -- "never uses
# ProcessBuilder" -- calls nothing. Unicode escapes are translated first, as javac does
# (JLS 3.3), so an escaped name is still seen and a `\u000a' still ends a `//' comment.
# String, char and text-block literals are kept as they are: a name spelled in one can be
# reached by reflection.
_JAVA_UESC = re.compile(r"(?<!\\)((?:\\\\)*)\\u+([0-9a-fA-F]{4})")
_JAVA_LEX = re.compile(r'"""(?:\\.|[^\\])*?"""|"(?:\\.|[^"\\\n])*"?|'
                       r"'(?:\\.|[^'\\\n])*'?|//[^\n]*|/\*.*?(?:\*/|\Z)", re.S)


def java_code(text):
    text = _JAVA_UESC.sub(lambda m: m.group(1) + chr(int(m.group(2), 16)), text)
    return _JAVA_LEX.sub(lambda m: re.sub(r"[^\n]", " ", m.group(0))
                         if m.group(0).startswith("/") else m.group(0), text)
# Native executable magics. 0xCAFEBABE (a .class, and also a Mach-O fat header) is deliberately
# excluded so legitimate JVM bytecode never trips this; a faithful build emits only .class files
# and shell launchers, so any of these under the build tree -- or left in a scratch dir -- means a
# foreign native tool was vendored or fetched to be wrapped behind a launcher.
NATIVE_MAGIC = (b"\x7fELF", b"\x00asm", b"\xcf\xfa\xed\xfe",
                b"\xfe\xed\xfa\xce", b"\xfe\xed\xfa\xcf", b"MZ")
SCRATCH_DIRS = ("/tmp", "/dev/shm", "/var/tmp", "/run/lock", "/dev/mqueue")


def check_language():
    """INTERFACE section 1: Java built by this JDK, no third-party native code.

    Enforces exactly what the contract states -- no sources of another compiled
    language, no loading of a library other than the platform C library, and no
    build step that reaches for another toolchain or the network.
    """
    bad = []
    for root, dirs, files in os.walk(WORK):
        dirs[:] = [d for d in dirs if d not in ("build", "bin")]
        for f in files:
            path = os.path.join(root, f)
            rel = os.path.relpath(path, WORK)
            if f.endswith(FORBIDDEN_SRC):
                bad.append("%s: source of another compiled language" % rel)
                continue
            if f.endswith(".java"):
                try:
                    text = java_code(open(path, errors="replace").read())
                except OSError:
                    continue
                if FORBIDDEN_NATIVE.search(text):
                    bad.append("%s: loads a native library" % rel)
                if FORBIDDEN_EXEC.search(text):
                    bad.append("%s: spawns an external process" % rel)
            elif f in ("Makefile", "makefile", "GNUmakefile") or f.endswith((".mk", ".sh")):
                is_make = (f in ("Makefile", "makefile", "GNUmakefile")
                           or f.endswith(".mk"))
                rx = FORBIDDEN_CALL_MAKE if is_make else FORBIDDEN_CALL
                try:
                    text = open(path, errors="replace").read()
                except OSError:
                    continue
                for i, line in enumerate(text.split("\n"), 1):
                    if line.lstrip().startswith("#"):
                        continue
                    m = rx.search(line)
                    if m:
                        bad.append("%s:%d: build invokes %s" % (rel, i, m.group(1)))
    return bad


def _has_native_magic(path):
    # Read without following a symlink and without blocking: a candidate build could leave a FIFO
    # or device where this scan walks (WORK, a scratch dir), and a blocking open() on it would hang
    # this root scan until the outer timeout. Only a regular file can be a vendored executable, so
    # anything else is simply not native.
    try:
        fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
    except OSError:
        return False
    try:
        if not stat.S_ISREG(os.fstat(fd).st_mode):
            return False
        magic = os.read(fd, 4)
    except OSError:
        return False
    finally:
        os.close(fd)
    return any(magic.startswith(m) for m in NATIVE_MAGIC)


def check_foreign():
    """No native executable anywhere the candidate could have placed one.

    The contract is a Java toolkit built by this JDK: the build emits .class bytecode and shell
    launchers that exec `java`. Wrapping a real readelf/eu-stack/gdbserver requires that native
    binary to exist as a file the candidate wrote -- in the build tree, or in a world-writable
    scratch directory at build time. This finds it in either place; reaching it from Java
    (Runtime.exec/ProcessBuilder) is caught separately by check_language.
    """
    bad = []
    for root, dirs, files in os.walk(WORK):
        for f in files:
            p = os.path.join(root, f)
            if not os.path.islink(p) and _has_native_magic(p):
                bad.append("%s: native executable in the build tree" % os.path.relpath(p, WORK))
    # A scratch dir is world-writable, so only the candidate's own executable binaries are
    # suspect; the JDK and python leave non-executable temp files, not executable ELF.
    for d in SCRATCH_DIRS:
        for root, dirs, files in os.walk(d):
            for f in files:
                p = os.path.join(root, f)
                try:
                    st = os.lstat(p)
                except OSError:
                    continue
                import stat as _stat
                if not _stat.S_ISREG(st.st_mode):
                    continue
                if st.st_uid != SCORE_UID or not (st.st_mode & 0o111):
                    continue
                if _has_native_magic(p):
                    bad.append("%s: native executable left in a scratch dir" % p)
            if len(bad) > 20:
                return bad
    return bad


def build():
    t0 = time.time()
    env = dict(os.environ)
    env["PATH"] = os.environ.get("PATH", "")
    try:
        p = subprocess.run(dropped(["make"], env), cwd=WORK, capture_output=True, text=True,
                           timeout=BUILD_TIMEOUT, env=ROOT_ENV)
    except subprocess.TimeoutExpired:
        log("build: TIMEOUT after %ds" % BUILD_TIMEOUT)
        return False
    log("build: exit=%d in %.1fs" % (p.returncode, time.time() - t0))
    tail = (p.stdout + p.stderr).strip().splitlines()[-25:]
    for line in tail:
        log("  | " + line)
    return p.returncode == 0


def normalize(text, mode):
    if mode == "ws":
        return "\n".join(re.sub(r"[ \t]+", " ", l).rstrip() for l in text.split("\n"))
    return "\n".join(l.rstrip() for l in text.split("\n"))


def split_units(text, mode):
    if mode == "blank":
        return [b for b in text.strip("\n").split("\n\n")]
    if mode == "addrline":
        out, cur = [], []
        for l in text.split("\n"):
            if re.match(r"^0x[0-9a-f]+$", l):
                if cur:
                    out.append("\n".join(cur))
                cur = [l]
            elif l or cur:
                cur.append(l)
        if cur:
            out.append("\n".join(cur))
        return out
    if mode == "line":
        return [l for l in text.split("\n") if l.strip()]
    if mode == "fdeblock":
        lines = text.split("\n")
        i = 0
        while i < len(lines) and not re.match(
                r"^Contents of the \.(eh_frame|debug_frame) section", lines[i]):
            i += 1
        body = "\n".join(lines[i:])
        return [b for b in body.strip("\n").split("\n\n") if b.strip()]
    return [text]


GDB_STACK = re.compile(r"0x7[0-9a-f]{9,15}")
GDB_PID = re.compile(r"process \d+|LWP \d+|Thread \d+\.\d+|thread \d+\.\d+")
GDB_STACKROW = re.compile(r"^(\s*)(\w+)(\s+)<STACK>(\s+)(-?\d+)\s*$")
GDB_REGROW = re.compile(r"^(\s*)(rbx)(\s+)(?:<STACK>|0x[0-9a-f]+)(\s+)(-?\d+)\s*$")
GDB_DROP = re.compile(r"^(Reading symbols|Remote debugging|warning: |\[New |\[Switching|"
                      r"Detaching|Ending remote|\[Inferior|Continuing\.|Run till exit)")


def normalise_gdb(text):
    out = []
    for line in text.split("\n"):
        if GDB_DROP.search(line):
            continue
        line = GDB_STACK.sub("<STACK>", line)
        line = GDB_PID.sub("<TID>", line)
        line = GDB_STACKROW.sub(r"\1\2 <STACK> <VAL>", line)
        line = GDB_REGROW.sub(r"\1\2 <VAL> <VAL>", line)
        out.append(line.rstrip())
    return "\n".join(l for l in out if l.strip())


def run_gdb_case(case, binroot, env):
    """Drive the candidate's bin/gdbserver with the real gdb, as recorded."""
    fx = os.path.join(FXROOT, "fx", case["fixture"])
    port = int(case["port"])
    script = case["gdb_script"].replace("@FX@", fx)
    # The gdb CLIENT below runs as root (it is the real debugger driving the candidate's
    # gdbserver). Its -x script must live where the candidate uid cannot reach it: WORK is owned by
    # the candidate uid, so a gdbserver left running with cwd there could unlink this root-owned
    # file and recreate it with its own commands -- which root's gdb would then execute (a root
    # shell out of the candidate uid, and a forged reward). A root-only 0700 scratch dir, which the candidate
    # uid cannot enter, closes that. The gdb client also runs with its cwd in that safe dir so it
    # never looks in WORK for anything. The gdbserver (candidate code) still runs from WORK.
    sdir = tempfile.mkdtemp(prefix="gdbx.")
    spath = os.path.join(sdir, "gdb.script")
    with open(spath, "w") as fh:
        fh.write(script)
    srv = subprocess.Popen(dropped([os.path.join(binroot, "bin", "gdbserver"),
                                    "127.0.0.1:%d" % port, fx], env),
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                           env=ROOT_ENV, cwd=WORK)
    time.sleep(3.0)
    try:
        # root's own process: ROOT_ENV, never the case env whose PATH heads with the shims
        p = subprocess.run([GDB_CLIENT, "-batch", "-nx", "-x", spath, fx],
                           capture_output=True, text=True, timeout=CASE_TIMEOUT, env=ROOT_ENV,
                           cwd=sdir)
        out = p.stdout
    except subprocess.TimeoutExpired:
        out = "<<TIMEOUT>>"
    except Exception as e:
        out = "<<ERROR %s>>" % e
    finally:
        try:
            srv.kill()
            srv.wait(timeout=10)
        except Exception:
            pass
        shutil.rmtree(sdir, ignore_errors=True)
    return normalise_gdb(out)


def run_case(case, binroot, env):
    tool = case["tool"]
    exe = os.path.join(binroot, "bin", tool)
    fx = os.path.join(FXROOT, "fx", case["fixture"]) if case.get("fixture") else ""
    core = os.path.join(FXROOT, "cores", case["core"] + ".core") if case.get("core") else ""
    args = [a.replace("@FX@", fx).replace("@CORE@", core) for a in case["args"]]
    stdin = case.get("stdin", "").replace("@FX@", fx).replace("@CORE@", core)
    try:
        p = subprocess.run(dropped([exe] + args, env), input=stdin, capture_output=True,
                           text=True, timeout=CASE_TIMEOUT, env=ROOT_ENV, cwd=WORK)
        return p.stdout
    except subprocess.TimeoutExpired:
        return "<<TIMEOUT>>"
    except Exception as e:
        return "<<ERROR %s>>" % e


def score_group(path, binroot, env, detail):
    data = json.load(open(path))
    matched = total = 0
    for case in data["cases"]:
        if out_of_time():
            total += len(split_units(normalize(case["expect"], case.get("normalize", "none")),
                                     case.get("split", "line")))
            continue
        budget = case.get("budget_sec")
        t_case = time.time()
        if case.get("normalize") == "gdb":
            exp = case["expect"]
            act = run_gdb_case(case, binroot, env)
        else:
            exp = normalize(case["expect"], case.get("normalize", "none"))
            act = normalize(run_case(case, binroot, env), case.get("normalize", "none"))
        elapsed = time.time() - t_case
        if budget is not None and elapsed > budget:
            eu = split_units(exp, case["split"])
            detail.append("  %-46s 0/%-4d over budget (%.1fs > %ds)"
                          % (case["id"], len(eu), elapsed, budget))
            total += len(eu) or 1
            continue
        eu = split_units(exp, case["split"])
        au = split_units(act, case["split"])
        m = sum(1 for i in range(min(len(eu), len(au))) if eu[i] == au[i])
        d = max(len(eu), len(au)) or 1
        matched += m
        total += d
        if m != d:
            detail.append("  %-46s %4d/%-4d" % (case["id"], m, d))
    return matched, total


def main():
    os.makedirs(LOGDIR, exist_ok=True)
    os.makedirs(WORK, exist_ok=True)
    if not (SETPRIV and ENV_BIN):
        write_reward(0.0, "setpriv/env unavailable: refusing to run candidate code privileged")
        return
    weights = json.load(open(os.path.join(TESTS, "weights.json")))
    n = stage_sources()
    log("staged %d text files from %s" % (n, APP))
    # The candidate's committed /app tree is staged into WORK text-only (stage_sources skips
    # binaries), so a native tool committed under /app is never copied and never reaches
    # check_foreign -- yet it stays on disk, and the scored Java could FFM-execvp it by absolute
    # path. Nothing the candidate runs needs /app after staging: the build and every scored run
    # happen in WORK. So lock /app to root-only; uid SCORE_UID then cannot traverse into it to
    # reach, let alone exec, anything there. Only the top directory is touched (no recursion), so
    # no candidate-planted symlink is ever followed.
    try:
        os.chown(APP, 0, 0)
        os.chmod(APP, 0o700)
    except OSError as exc:
        log("note: could not lock %s from the candidate uid: %r" % (APP, exc))
    for root, dirs, files in os.walk(WORK):
        for name in dirs + files:
            try:
                os.chown(os.path.join(root, name), SCORE_UID, SCORE_UID)
            except OSError:
                pass
    try:
        os.chown(WORK, SCORE_UID, SCORE_UID)
    except OSError:
        pass
    violations = check_language()
    if violations:
        for v in violations[:10]:
            log("language check: " + v)
        write_reward(0.0, "workspace violates the stated implementation constraints")
        return
    if not build():
        write_reward(0.0, "build failed")
        return
    foreign = check_foreign()
    if foreign:
        for v in foreign[:10]:
            log("foreign-binary check: " + v)
        write_reward(0.0, "a native tool was vendored or fetched into the build tree")
        return
    required = weights.get("binaries", ["llvm-symbolizer", "readelf", "eu-stack"])
    notjava = []
    for b in required:
        path = os.path.join(WORK, "bin", b)
        if not os.access(path, os.X_OK):
            continue
        try:
            head = open(path, "rb").read(8192)
        except OSError:
            head = b""
        if b"java" not in head:
            notjava.append(b)
    if notjava:
        log("bin/%s is not a JDK launcher" % ", bin/".join(notjava))
        write_reward(0.0, "produced executables are not JDK launchers")
        return
    gate = [b for b in required if b != "gdbserver"]
    missing = [b for b in gate if not os.access(os.path.join(WORK, "bin", b), os.X_OK)]
    if missing:
        log("missing executables after make: %s" % ", ".join(missing))
        write_reward(0.0, "missing " + ",".join(missing))
        return
    poison_dir, tattle = make_poison()
    env = dict(os.environ)
    env["PATH"] = poison_dir + ":/usr/lib/jvm/java-25-openjdk-amd64/bin:/usr/bin:/bin"
    env["LC_ALL"] = "C"
    env["TZ"] = "UTC"
    # A debugger client, where one is present in this image, must not be
    # reachable while the symbolizer and unwinder groups run.
    hidden = []
    for g in ("/usr/bin/gdb", "/usr/bin/gdbserver"):
        if os.path.exists(g) and not weights.get("needs_gdb"):
            try:
                shutil.move(g, g + ".hidden")
                hidden.append(g)
            except OSError as e:
                log("note: could not hide %s (%s)" % (g, e))

    # Product scoring: reward = T1 x T2.
    #   T1 (milestone) = weighted mean over this step's own groups -- every group except
    #                    "regress" -- with their existing relative weights renormalised to [0,1].
    #   T2 (inherited) = the "regress" group's own pass fraction (the earlier-version cases
    #                    carried forward), or 1.0 when this step ships no regress group (step 01).
    # A tree that implements only the previous version passes regress (T2) but scores T1 = 0, so
    # its reward is 0.
    t1_num = 0.0      # milestone: sum of weight * fraction over non-regress groups
    t1_w = 0.0        # milestone: total weight of non-regress groups (renormalisation divisor)
    t2_frac = None    # inherited: the regress group's fraction, if this step has one
    detail = []
    summary = {}
    for group, w in weights["groups"].items():
        path = os.path.join(TESTS, "exp", group + ".json")
        if not os.path.exists(path):
            log("group %s: no expectation file" % group)
            continue
        detail.append("group %s (weight %.2f)" % (group, w))
        t0 = time.time()
        m, t = score_group(path, WORK, env, detail)
        frac = (m / t) if t else 0.0
        summary[group] = (m, t, frac, w)
        if group == "regress":
            t2_frac = frac
        else:
            t1_num += w * frac
            t1_w += w
        log("group %-14s %6d/%-6d = %.4f  (weight %.2f)  %.1fs"
            % (group, m, t, frac, w, time.time() - t0))
    for g in hidden:
        try:
            shutil.move(g + ".hidden", g)
        except OSError:
            pass

    if os.path.exists(tattle) and os.path.getsize(tattle) > 0:
        with open(tattle) as fh:
            calls = fh.read().strip().splitlines()
        log("POISON: candidate invoked %d oracle tool(s): %s" % (len(calls), calls[:5]))
        write_reward(0.0, "invoked an oracle tool at scoring time")
        return

    # reward = T1 (milestone) x T2 (inherited). T1 is the weighted mean over the non-regress
    # groups, renormalised; T2 is the regress group's fraction, or 1.0 when there is none (step 01).
    t1 = (t1_num / t1_w) if t1_w > 0 else 0.0
    t2 = t2_frac if t2_frac is not None else 1.0
    total_reward = t1 * t2
    log("reward = T1 %.4f (milestone over weight %.2f) x T2 %s (inherited) = %.4f"
        % (t1, t1_w,
           ("%.4f" % t2_frac) if t2_frac is not None else "1.0 (no regress group)",
           total_reward))

    with open(os.path.join(LOGDIR, "groups.txt"), "w") as fh:
        for g, (m, t, frac, w) in summary.items():
            fh.write("%s %d %d %.6f %.2f\n" % (g, m, t, frac, w))
    with open(os.path.join(LOGDIR, "detail.txt"), "w") as fh:
        fh.write("\n".join(detail) + "\n")
    write_reward(total_reward, "product T1 x T2 over %d groups" % len(summary))


try:
    main()
except Exception as exc:  # a verifier crash must still leave a reward behind
    import traceback
    log("verifier error: %s" % exc)
    log(traceback.format_exc()[-2000:])
    write_reward(0.0, "verifier error")
