#!/usr/bin/env python3
"""Run the scored corpus against the candidate JVM and report the pass rate.

Every test directory holds the class files, the exact `java` arguments, and a
digest of the behaviour recorded from a reference OpenJDK build at authoring
time.  The recording itself is not shipped: a candidate that fingerprinted the
class files it was handed, found the directory they came from and replayed the
recording beside them would implement no virtual machine and score 1.000000.
Only a SHA-256 of each recording is shipped, and it cannot be inverted.

The candidate also runs as an unprivileged uid under an address-space ceiling,
so it can neither read the scorer's own files nor take the pod down with it.
Both guards assert themselves before the corpus runs.
"""
import concurrent.futures, hashlib, importlib.util, json, os, pathlib, re, shutil, subprocess, sys, tempfile, threading, time

VM = "/app/jvm/bin/java"
FRAME = re.compile(r"^\tat ([\w.$/]+)\.")

CHILD_UID = 65534                 # nobody:nogroup
AS_BYTES = 2 * 1024 * 1024 * 1024  # per-case address space; 7 at a time, pod has 16 GiB
SETPRIV, PRLIMIT = "/usr/bin/setpriv", "/usr/bin/prlimit"
SANDBOX = os.geteuid() == 0 and os.path.exists(SETPRIV) and os.path.exists(PRLIMIT)


def wrap(argv):
    """The candidate never runs as the scorer."""
    if not SANDBOX:
        return list(argv)
    return [SETPRIV, f"--reuid={CHILD_UID}", f"--regid={CHILD_UID}", "--clear-groups",
            "--no-new-privs", "--", PRLIMIT, f"--as={AS_BYTES}", "--core=0", "--"] + list(argv)


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


def digest(code, out, err):
    """Canonical digest of one behaviour, in the form expected.sha256 holds."""
    blob = json.dumps([code, out, err], ensure_ascii=False, sort_keys=True).encode("utf-8")
    return hashlib.sha256(blob).hexdigest()


def child_env():
    env = dict(os.environ)
    env["PATH"] = "/decoy:/usr/bin:/bin"
    env["LANG"] = "C"
    env["LC_ALL"] = "C"
    env["HOME"] = "/tmp"
    env.pop("JAVA_HOME", None)
    return env


DEADLINE = None          # set in main(); the whole corpus has to finish inside it


def run_one(d, timeout):
    # A candidate that hangs must not spend the budget belonging to the families
    # it has not reached yet: sixty mutations first and a thread test last is how
    # a carry-forward set silently scores zero for a reason unrelated to it.
    if DEADLINE is not None:
        left = DEADLINE - time.monotonic()
        if left <= 1:
            return d.name, False, "not reached"
        timeout = min(timeout, left)
    spec = json.loads((d / "cmd").read_text())
    want = (d / "expected.sha256").read_text().strip()
    tmp = tempfile.mkdtemp(prefix="t-", dir="/run-corpus")
    try:
        for f in d.iterdir():
            if f.name in ("cmd", "expected.sha256"):
                continue
            if f.is_dir():
                shutil.copytree(f, pathlib.Path(tmp) / f.name)
            else:
                shutil.copy(f, tmp)
        # the case is the candidate's working directory, so it has to own it
        subprocess.run(["chmod", "-R", "a+rwX", tmp], check=False)
        cmd = wrap([VM] + spec.get("vmargs", []) + spec["args"])
        try:
            p = subprocess.run(cmd, cwd=tmp, env=child_env(),
                               capture_output=True, timeout=timeout)
            code, so, se = p.returncode, p.stdout, p.stderr
        except subprocess.TimeoutExpired:
            return d.name, False, "timeout"
        got_out = so.decode("utf-8", errors="surrogateescape")
        got_err = filter_err(se.decode("utf-8", errors="surrogateescape"))
        if digest(code, got_out, got_err) != want:
            return d.name, False, "behaviour"
        return d.name, True, ""
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def selfcheck(roots):
    """A guard that is not asserted is a guard that is not there."""
    ok, leak = True, False
    if not SANDBOX:
        print("GUARD sandbox UNAVAILABLE (not root, or setpriv/prlimit missing)")
        ok = False
    else:
        p = subprocess.run(wrap(["/usr/bin/id", "-u"]), capture_output=True, text=True)
        if p.stdout.strip() != str(CHILD_UID):
            print("GUARD uid FAIL (child ran as %r)" % p.stdout.strip())
            ok = False
        else:
            print("GUARD uid ok (candidate runs as %d)" % CHILD_UID)
        # no_new_privs: an exec cannot raise the child's privileges, so neither a
        # set-id bit nor a file capability on anything it runs takes effect
        p = subprocess.run(wrap(["/bin/grep", "^NoNewPrivs:", "/proc/self/status"]),
                           capture_output=True, text=True)
        if p.stdout.split()[-1:] != ["1"]:
            print("GUARD nnp FAIL (child runs without no_new_privs: %r)" % p.stdout.strip())
            ok = False
        else:
            print("GUARD nnp ok (an exec cannot raise the candidate's privileges)")
        victim = next((c / "expected.sha256" for r in roots
                       for c in sorted(pathlib.Path(r).iterdir())
                       if (c / "expected.sha256").exists()), None)
        if victim is not None:
            p = subprocess.run(wrap(["/bin/cat", str(victim)]), capture_output=True, text=True)
            if p.returncode == 0:
                print("GUARD read FAIL (child read %s)" % victim)
                leak = True
            else:
                print("GUARD read ok (child cannot read the answer key)")
    leaked = [str(p) for r in roots for p in pathlib.Path(r).rglob("expected.json")]
    if leaked:
        print("GUARD key FAIL (recorded behaviour shipped: %s)" % leaked[:3])
        leak = True
    else:
        print("GUARD key ok (only digests are shipped)")
    # A missing sandbox is a weaker image, and the digest still carries the
    # defence, so it scores.  A readable answer key is not weakness, it is a
    # meaningless score, and it stops here.
    print("GUARDS %s" % ("LEAK" if leak else ("ok" if ok else "DEGRADED")))
    return not leak


def load_scanner():
    """/tests/foreign.py, by path: under -I this script's own directory is not on
    sys.path, and nothing the candidate can write ever is."""
    spec = importlib.util.spec_from_file_location("foreign", "/tests/foreign.py")
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


class Watch(threading.Thread):
    """Another Java runtime has to be on disk while a case runs on it, so the
    directories the candidate uid can write are inspected before the first case,
    every few seconds while the cases run, and once more after the last."""

    def __init__(self, scanner, period=5.0):
        super().__init__(daemon=True)
        self.scanner, self.period = scanner, period
        self.halt = threading.Event()
        self.found, self.scans, self.complete = {}, 0, True

    def once(self):
        found, complete, _ = self.scanner.scan()
        for path, why in found:
            self.found.setdefault(path, why)
        self.complete = self.complete and complete
        self.scans += 1

    def run(self):
        while not self.halt.wait(self.period):
            self.once()

    def finish(self):
        self.halt.set()
        self.join(120)
        if self.is_alive():
            self.complete = False
        else:
            self.once()
        for path, why in sorted(self.found.items())[:20]:
            print("FOREIGN %s -- %s" % (path, why))
        print("SCAN %s %d" % ("ok" if self.complete else "incomplete", self.scans))


def family(name):
    """The behaviour a case belongs to.

    Cases are scored a family at a time rather than one by one: a corpus with
    sixty class-file mutations and one thread test should not let a program that
    prints `java.lang.ClassFormatError` and exits collect more than half the
    milestone. The rung a regression case came from stays part of its family, so
    rung 1's arithmetic and rung 2's do not merge.
    """
    if "__" in name:
        head, rest = name.split("__", 1)
        return head + "__" + rest.split("-")[0]
    return name.split("-")[0]


def main():
    timeout = float(sys.argv[1])
    roots = sys.argv[2:]
    # Root's and 0755: each case directory inside is opened up to the candidate,
    # the parent is not. A parent the candidate uid can write lets a process left
    # over from one case rename the next case's directory and plant a symlink in
    # its place before root runs chmod -R on that path.
    os.makedirs("/run-corpus", exist_ok=True)
    if os.geteuid() == 0:
        os.chown("/run-corpus", 0, 0)
    os.chmod("/run-corpus", 0o755)
    selfcheck(roots)
    watch = Watch(load_scanner())
    watch.once()
    watch.start()
    global DEADLINE
    DEADLINE = time.monotonic() + float(os.environ.get("CORPUS_BUDGET_SEC", "1200"))
    tests, owner = [], {}
    for r in roots:
        for d in sorted(pathlib.Path(r).iterdir()):
            if d.is_dir() and (d / "cmd").exists():
                tests.append(d)
                owner[str(d)] = r
    # interleave the families, so every behaviour is reached early and a slow one
    # costs itself rather than everything that would have come after it
    buckets = {}
    for d in tests:
        buckets.setdefault((owner[str(d)], family(d.name)), []).append(d)
    order, keys = [], sorted(buckets)
    while any(buckets[k] for k in keys):
        for k in keys:
            if buckets[k]:
                order.append(buckets[k].pop(0))
    tests = order
    passed, failed = 0, []
    per = {r: [0, 0] for r in roots}
    fams = {}
    with concurrent.futures.ThreadPoolExecutor(max_workers=7) as ex:
        for d, (name, ok, why) in zip(tests, ex.map(lambda d: run_one(d, timeout), tests)):
            r = owner[str(d)]
            per[r][1] += 1
            slot = fams.setdefault((r, family(name)), [0, 0])
            slot[1] += 1
            if ok:
                passed += 1
                per[r][0] += 1
                slot[0] += 1
            else:
                failed.append("%s (%s)" % (name, why))
    watch.finish()
    unreached = sum(1 for f in failed if f.endswith("(not reached)"))
    if unreached:
        print("corpus: %d cases were not reached inside the budget and count as failures"
              % unreached)
    print("corpus: %d/%d passed" % (passed, len(tests)))
    for f in failed[:120]:
        print("  FAIL " + f)
    for (r, f), (p_, t_) in sorted(fams.items()):
        print("FAM %s %s %d %d" % (r, f, p_, t_))
    for r in roots:
        print("ROOT %s %d %d" % (r, per[r][0], per[r][1]))
    print("SCORE %d %d" % (passed, len(tests)))


main()
