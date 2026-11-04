#!/usr/bin/env python3
"""Sample-corpus runner. Compares stdout, exit status and the first stderr line
plus any stack frames outside the platform library."""
import json, os, pathlib, re, shutil, subprocess, sys, tempfile

HERE = pathlib.Path(__file__).resolve().parent
VM = "/app/jvm/bin/java"
FRAME = re.compile(r"^\tat ([\w.$/]+)\.")

# Each test runs in a directory of its own, as an unprivileged user, under a
# two-gibibyte address-space ceiling, with no JDK reachable.  This runner sets
# those conditions up wherever it can, so that a pass with it is a pass under
# them: a launcher that needs to be root, writes next to itself, shells out to
# the JDK in this image, or reserves more address space than the ceiling fails
# its tests.
CHILD_UID = 65534
AS_BYTES = 2 * 1024 * 1024 * 1024
SETPRIV, PRLIMIT = "/usr/bin/setpriv", "/usr/bin/prlimit"
SANDBOX = os.geteuid() == 0 and os.path.exists(SETPRIV) and os.path.exists(PRLIMIT)


def decoy_path():
    """Put a decoy `java` first on the path for the duration of a test, so a
    launcher that shells out to a real JVM is caught instead of passing. This
    image does carry a JDK -- on purpose, so you can generate your own examples
    with it -- and the decoy goes in front of it."""
    d = pathlib.Path(tempfile.gettempdir()) / "no-jdk"
    d.mkdir(exist_ok=True)
    for name in ("java", "javac", "jshell", "jar"):
        f = d / name
        if not f.exists():
            f.write_text('#!/bin/sh\necho "the launcher started another JVM: $0" >&2\nexit 42\n')
            f.chmod(0o755)
    return "%s:/usr/bin:/bin" % d


def wrap(argv):
    if not SANDBOX:
        return list(argv)
    return [SETPRIV, "--reuid=%d" % CHILD_UID, "--regid=%d" % CHILD_UID,
            "--clear-groups", "--", PRLIMIT, "--as=%d" % AS_BYTES, "--core=0",
            "--"] + list(argv)

def filter_err(text):
    lines = text.splitlines()
    out = []
    for i, ln in enumerate(lines):
        if i == 0:
            out.append(ln)
            continue
        m = FRAME.match(ln)
        if m:
            cls = m.group(1)
            if cls.startswith("java.") or cls.startswith("jdk.") or cls.startswith("sun."):
                continue
            out.append(ln)
        elif ln.startswith("Caused by:"):
            # the text of a linkage error is not specified; keep only its class
            out.append(re.sub(r"^(Caused by: [\w.$]+).*$", r"\1", ln))
        elif ln.startswith("\t... "):
            out.append(ln)
    return "\n".join(out).rstrip() + ("\n" if out else "")

def main():
    pat = sys.argv[1] if len(sys.argv) > 1 else ""
    if SANDBOX:
        # Make the build output legible to the unprivileged uid before any test
        # runs, or every test fails on permissions rather than on behaviour.
        subprocess.run(["chmod", "-R", "a+rX", "/app/jvm"], check=False)
    if not SANDBOX:
        print("(no sandbox here: the tests must also pass when each runs as an "
              "unprivileged user under a 2 GiB ceiling)")
    roots = [p for p in sorted(HERE.iterdir()) if p.is_dir()]
    tests = []
    for r in roots:
        for d in sorted(r.iterdir()):
            if d.is_dir() and (d / "cmd").exists() and pat in d.name:
                tests.append(d)
    npass = 0
    for d in tests:
        spec = json.loads((d / "cmd").read_text())
        exp = json.loads((d / "expected.json").read_text())
        tmp = tempfile.mkdtemp(prefix="t-")
        for f in d.iterdir():
            if f.name in ("cmd", "expected.json"):
                continue
            if f.is_dir():
                shutil.copytree(f, pathlib.Path(tmp) / f.name)
            else:
                shutil.copy(f, tmp)
        if SANDBOX:
            subprocess.run(["chmod", "-R", "a+rwX", tmp], check=False)
        env = dict(os.environ)
        env["LANG"] = env["LC_ALL"] = "C"
        env["PATH"] = decoy_path()
        env["HOME"] = "/tmp"
        env.pop("JAVA_HOME", None)
        try:
            p = subprocess.run(wrap([VM] + spec.get("vmargs", []) + spec["args"]), cwd=tmp,
                               env=env, capture_output=True, timeout=90)
            code, so, se = p.returncode, p.stdout, p.stderr
        except subprocess.TimeoutExpired:
            code, so, se = 124, b"", b"TIMEOUT"
        except FileNotFoundError:
            print("no %s -- build it first with: make -C /app/jvm" % VM)
            return 2
        got = (code, so.decode("utf-8", "surrogateescape"),
               filter_err(se.decode("utf-8", "surrogateescape")))
        want = (exp["code"], exp["out"], exp["err"])
        shutil.rmtree(tmp, ignore_errors=True)
        if got == want:
            npass += 1
        else:
            print("FAIL %s" % d.name)
            for i, f in enumerate(("exit", "stdout", "stderr")):
                if got[i] != want[i]:
                    print("   expected %s: %r" % (f, want[i]))
                    print("   actual   %s: %r" % (f, got[i]))
    print("%d/%d sample tests passed" % (npass, len(tests)))
    return 0 if npass == len(tests) else 1

sys.exit(main())
