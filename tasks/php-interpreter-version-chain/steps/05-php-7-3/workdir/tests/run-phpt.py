#!/usr/bin/env python3
"""run-phpt.py — a .phpt runner for a PHP CLI binary.

This is a faithful re-implementation of the *tested-binary contract* of
php-src's own `run-tests.php`: the same command line, the same `.phpt`
sections, the same `EXPECTF` placeholder semantics and the same pass/fail
rule.  php-src's `run-tests.php` is itself written in PHP and therefore cannot
be used to bootstrap a PHP interpreter that does not exist yet; everything it
does to the binary under test is reproduced here.

Contract, as `run-tests.php` implements it:

  command   <php> -n <ini options> -f <test>.php [-- <ARGS>]   with 2>&1
  ini       one `-d name=value` per entry of php-src's `$ini_overwrites`
            (read from the `run-tests-ini.txt` next to the test pool) followed
            by one `-d name=value` per line of the test's `--INI--` section
  stdin     the `--STDIN--` section, otherwise empty
  env       the `--ENV--` section's `name=value` lines, over a fixed base
  compare   output is trimmed (" \\t\\n\\r\\0\\x0B") and CRLF-normalised, then
            `--EXPECT--`  exact string equality
            `--EXPECTF--` converted to a regex (`%s %S %a %A %w %i %d %x %f
                          %c %e %0` and `%r…%r`) and matched with `^…$` and
                          the PCRE `/s` flag
            `--EXPECTREGEX--` matched the same way without conversion
  `{PWD}` in `--INI--` and `--ENV--` expands to the test's directory.

The generated `<name>.php` is written next to its `.phpt` and run with that
directory as the working directory, exactly as `run-tests.php` does, so a test
that includes a neighbouring `.inc` file works.  The pool is copied to a
scratch directory first so the original tree is never written to.

`--SKIPIF--` is deliberately not executed: a partially finished interpreter
cannot be trusted to decide which tests it may skip, so every selected test
runs.  A test carrying `--XFAIL--` is not run; the runner reports it as
XFAIL.

Usage:
  run-phpt.py --php /app/sapi/cli/php --root <dir> [--list FILE] [PATH ...]
              [--jobs N] [--timeout SEC] [--memory-cap-mb MB]
              [--json OUT] [--show-diff N]
"""

import argparse
import concurrent.futures
import json
import os
import re
import subprocess
import sys
import tempfile
import shutil
import threading

# A test is run with a ceiling on its resident memory.  The binary under test
# is an interpreter someone is in the middle of writing: a runaway allocation
# in it must fail that one test, not exhaust the machine running the suite.
#
# The ceiling is on resident memory rather than on address space, because those
# are very different numbers for an interpreter and only the first is what the
# machine is actually charged for -- a coroutine-per-generator design reserves
# gigabytes of stack it never touches, and an RLIMIT_AS that such a design
# could live with would be too loose to bound anything.  A test over the
# ceiling is killed and reported as a failure with the reason given.
DEFAULT_MEMORY_CAP_MB = 2048


def usable_cpus():
    """The number of CPUs this process may actually use.

    `os.cpu_count()` reports the machine's CPUs, which inside a container is
    the host's count and not the share the container was given.  Running one
    test per host CPU in a 7-CPU container oversubscribes it by an order of
    magnitude, so consult the cgroup CPU quota and the affinity mask too and
    take the smallest answer any of them gives.
    """
    counts = []
    try:
        counts.append(len(os.sched_getaffinity(0)))
    except (AttributeError, OSError):
        n = os.cpu_count()
        if n:
            counts.append(n)
    # cgroup v2: "<quota> <period>", quota may be the string "max"
    try:
        with open("/sys/fs/cgroup/cpu.max", encoding="ascii") as fh:
            quota, period = (fh.read().split() + ["100000"])[:2]
        if quota != "max" and int(period) > 0:
            counts.append(max(1, int(int(quota) / int(period))))
    except (OSError, ValueError):
        pass
    # cgroup v1: quota and period in separate files, quota -1 means unlimited
    try:
        with open("/sys/fs/cgroup/cpu/cpu.cfs_quota_us", encoding="ascii") as fh:
            quota = int(fh.read().strip())
        with open("/sys/fs/cgroup/cpu/cpu.cfs_period_us", encoding="ascii") as fh:
            period = int(fh.read().strip())
        if quota > 0 and period > 0:
            counts.append(max(1, int(quota / period)))
    except (OSError, ValueError):
        pass
    return max(1, min(counts)) if counts else 4


_watchers = {}


def watch_memory(proc, cap_mb):
    """Kill `proc` if its resident memory goes over `cap_mb`.

    Returns an Event that is set if that happened.  A cap of 0 disables the
    watch and returns an Event that is never set.
    """
    hit = threading.Event()
    if not cap_mb or cap_mb <= 0:
        return hit
    cap = cap_mb * 1024 * 1024
    stop = threading.Event()
    _watchers[proc.pid] = stop

    def run():
        page = os.sysconf("SC_PAGE_SIZE")
        statm = "/proc/%d/statm" % proc.pid
        while not stop.wait(0.05):
            try:
                with open(statm, encoding="ascii") as fh:
                    resident = int(fh.read().split()[1]) * page
            except (OSError, ValueError, IndexError):
                return                      # it exited; nothing to police
            if resident > cap:
                hit.set()
                try:
                    proc.kill()
                except OSError:
                    pass
                return

    threading.Thread(target=run, daemon=True).start()
    return hit


def stop_watch(proc):
    stop = _watchers.pop(proc.pid, None)
    if stop is not None:
        stop.set()

PHP_TRIM = " \t\n\r\0\x0b"

SECTION_RE = re.compile(r"^--([_A-Z]+)--[^\n]*\n?", re.M)

# Sections this runner understands.  A test that carries any other section is
# reported as "unsupported" instead of being silently skipped.
KNOWN = {
    "TEST", "DESCRIPTION", "CREDITS", "FILE", "EXPECT", "EXPECTF",
    "EXPECTREGEX", "INI", "ARGS", "ENV", "STDIN", "SKIPIF", "CLEAN",
    "XFAIL", "CONFLICTS", "EXTENSIONS", "CAPTURE_STDIO", "WHITESPACE_SENSITIVE",
}
# Sections that make a test unrunnable through the plain CLI contract.
UNSUPPORTED = {
    "FILE_EXTERNAL", "EXPECT_EXTERNAL", "EXPECTF_EXTERNAL",
    "EXPECTREGEX_EXTERNAL", "POST", "POST_RAW", "GET", "COOKIE", "PUT",
    "GZIP_POST", "DEFLATE_POST", "EXPECTHEADERS", "CGI", "PHPDBG",
    "REDIRECTTEST", "HEADERS", "FILEEOF", "STDIN_EOF",
}


def parse_phpt(text):
    """Split a .phpt into an ordered dict of section name -> body."""
    out = {}
    marks = list(SECTION_RE.finditer(text))
    if not marks:
        return out
    for i, m in enumerate(marks):
        name = m.group(1)
        start = m.end()
        end = marks[i + 1].start() if i + 1 < len(marks) else len(text)
        body = text[start:end]
        if body.endswith("\n"):
            body = body[:-1]
        out[name] = body
    return out


def expectf_to_regex(wanted):
    """Port of run-tests.php's expectf_to_regex()."""
    wanted = wanted.replace("\r\n", "\n")
    temp = ""
    start_offset = 0
    length = len(wanted)
    while start_offset < length:
        start = wanted.find("%r", start_offset)
        if start != -1:
            end = wanted.find("%r", start + 2)
            if end == -1:
                end = start = length
        else:
            start = end = length
        temp += re.escape(wanted[start_offset:start])
        if end > start:
            temp += "(" + wanted[start + 2:end] + ")"
        start_offset = end + 2
    subs = [
        ("%e", re.escape(os.sep)),
        ("%s", "[^\\r\\n]+"),
        ("%S", "[^\\r\\n]*"),
        ("%a", ".+"),
        ("%A", ".*"),
        ("%w", "\\s*"),
        ("%i", "[+-]?\\d+"),
        ("%d", "\\d+"),
        ("%x", "[0-9a-fA-F]+"),
        ("%f", "[+-]?(?:\\d+|(?=\\.\\d))(?:\\.\\d+)?(?:[Ee][+-]?\\d+)?"),
        ("%c", "."),
        ("%0", "\\x00"),
    ]
    # strtr() replaces each position once, left to right, never rescanning the
    # replacement text.
    res = []
    i = 0
    while i < len(temp):
        for key, val in subs:
            if temp.startswith(key, i):
                res.append(val)
                i += len(key)
                break
        else:
            res.append(temp[i])
            i += 1
    return "".join(res)


def load_ini_overwrites(path):
    if not path or not os.path.exists(path):
        return []
    out = []
    with open(path, encoding="utf8") as fh:
        for line in fh:
            line = line.strip()
            if line and not line.startswith("#"):
                out.append(line)
    return out


class Result(object):
    __slots__ = ("name", "path", "status", "output", "expected", "cmd", "seconds")

    def __init__(self, name, path, status, output="", expected="", cmd="", seconds=0.0):
        self.name = name
        self.path = path
        self.status = status
        self.output = output
        self.expected = expected
        self.cmd = cmd
        self.seconds = seconds


def run_one(php, root, rel, ini_overwrites, timeout, workdir,
            memory_cap_mb=DEFAULT_MEMORY_CAP_MB, run_as=None):
    import time
    full = os.path.join(root, rel)   # read from the pristine tree
    try:
        with open(full, encoding="utf8", errors="surrogateescape") as fh:
            text = fh.read()
    except OSError as exc:
        return Result(rel, rel, "BORK", "cannot read: %s" % exc)
    sec = parse_phpt(text)
    name = (sec.get("TEST") or rel).strip()
    if "FILE" not in sec:
        return Result(rel, rel, "UNSUPPORTED", "no --FILE-- section")
    bad = [k for k in sec if k in UNSUPPORTED]
    if bad:
        return Result(rel, rel, "UNSUPPORTED", "sections: %s" % ",".join(sorted(bad)))
    if "XFAIL" in sec:
        return Result(rel, rel, "XFAIL", sec["XFAIL"].strip())
    kinds = [k for k in ("EXPECT", "EXPECTF", "EXPECTREGEX") if k in sec]
    if len(kinds) != 1:
        return Result(rel, rel, "BORK", "needs exactly one EXPECT/EXPECTF/EXPECTREGEX")

    tdir = os.path.dirname(os.path.join(workdir, rel))
    try:
        os.makedirs(tdir, exist_ok=True)
        if run_as is not None:
            d = workdir
            for part in os.path.dirname(rel).split(os.sep):
                if not part: continue
                d = os.path.join(d, part)
                try: os.chmod(d, 0o777)
                except OSError: pass
            try: os.chmod(tdir, 0o777)
            except OSError: pass
        base = os.path.basename(rel)[:-5] if rel.endswith(".phpt") else os.path.basename(rel)
        script = os.path.join(tdir, base + ".php")
        with open(script, "w", encoding="utf8", errors="surrogateescape") as fh:
            fh.write(sec["FILE"])
        if run_as is not None:
            try: os.chmod(script, 0o644)
            except OSError: pass

        ini_args = []
        for entry in ini_overwrites:
            ini_args += ["-d", entry]
        if "INI" in sec:
            for line in sec["INI"].replace("{PWD}", tdir).splitlines():
                line = line.strip()
                if line and not line.startswith("#"):
                    ini_args += ["-d", line]

        env = {
            "PATH": "/nonexistent",
            "TZ": "UTC",
            "LC_ALL": "C",
            "LANG": "C",
            "HOME": tdir,
            "TMPDIR": tdir,
            "TEMP": tdir,
            "REQUEST_METHOD": "GET",
            "CONTENT_TYPE": "",
            "CONTENT_LENGTH": "",
            "QUERY_STRING": "",
            "PATH_TRANSLATED": script,
            "SCRIPT_FILENAME": script,
            "REDIRECT_STATUS": "1",
            "HTTP_COOKIE": "",
        }
        if "ENV" in sec:
            for line in sec["ENV"].replace("{PWD}", tdir).splitlines():
                line = line.strip()
                if "=" in line:
                    k, v = line.split("=", 1)
                    if k.strip():
                        env[k.strip()] = v
        cmd = [php, "-n"] + ini_args + ["-f", script]
        if "ARGS" in sec and sec["ARGS"].strip():
            cmd += ["--"] + sec["ARGS"].split()
        stdin_data = sec.get("STDIN", "")

        started = time.time()
        try:
            kw = {}
            if run_as is not None:
                # Drop privilege for the binary under test.  It must not be able to
                # read the corpus:
                # a "PHP interpreter" that opens the .phpt next to its script and
                # prints the --EXPECT-- section implements no PHP yet passes.
                kw["user"] = run_as
                kw["group"] = run_as
                # clear supplementary groups too: Popen keeps the parent's otherwise,
                # so the child would still carry gid 0 and could read anything the root
                # group can.
                kw["extra_groups"] = []
            proc = subprocess.Popen(
                cmd, cwd=tdir, env=env,
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                **kw
            )
        except OSError as exc:
            return Result(name, rel, "FAIL", "cannot execute: %s" % exc, "", " ".join(cmd))

        over_ceiling = watch_memory(proc, memory_cap_mb)
        try:
            raw, _ = proc.communicate(
                input=stdin_data.encode("utf8", "surrogateescape"), timeout=timeout)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.communicate()
            if over_ceiling.is_set():
                return Result(name, rel, "FAIL",
                              "went over the %d MB memory ceiling" % memory_cap_mb,
                              "", " ".join(cmd), time.time() - started)
            return Result(name, rel, "TIMEOUT", "", "", " ".join(cmd), time.time() - started)
        finally:
            stop_watch(proc)
        if over_ceiling.is_set():
            return Result(name, rel, "FAIL",
                          "went over the %d MB memory ceiling" % memory_cap_mb,
                          "", " ".join(cmd), time.time() - started)
        seconds = time.time() - started

        out = raw.decode("utf8", "replace").replace("\r\n", "\n").strip(PHP_TRIM)
        if "EXPECT" in sec:
            wanted = sec["EXPECT"].strip(PHP_TRIM).replace("\r\n", "\n")
            passed = out == wanted
        else:
            key = "EXPECTF" if "EXPECTF" in sec else "EXPECTREGEX"
            wanted = sec[key].strip(PHP_TRIM).replace("\r\n", "\n")
            pattern = expectf_to_regex(wanted) if key == "EXPECTF" else wanted
            try:
                passed = re.match("(?s)^(?:" + pattern + ")$", out) is not None
            except re.error as exc:
                return Result(name, rel, "BORK", "bad regex: %s" % exc, wanted, " ".join(cmd))
        return Result(name, rel, "PASS" if passed else "FAIL", out, wanted,
                      " ".join(cmd), seconds)
    finally:
        try:
            os.unlink(script)
        except OSError:
            pass


def main(argv=None):
    ap = argparse.ArgumentParser(description="run php-src .phpt tests against a PHP CLI binary")
    ap.add_argument("--php", required=True, help="path to the PHP CLI binary under test")
    ap.add_argument("--root", default=".", help="directory the test paths are relative to")
    ap.add_argument("--list", dest="listfile", help="file with one test path per line")
    ap.add_argument("--ini", dest="inifile", default=None,
                    help="file with one ini overwrite per line (default: <root>/run-tests-ini.txt)")
    ap.add_argument("--jobs", "-j", type=int, default=usable_cpus(),
                    help="tests to run at once (default: the CPUs this container may use)")
    ap.add_argument("--run-as", type=int, default=None, metavar="UID",
                    help="run the binary under test as this uid")
    ap.add_argument("--memory-cap-mb", type=int, default=DEFAULT_MEMORY_CAP_MB,
                    metavar="MB",
                    help="ceiling on one test's resident memory, 0 to disable "
                         "(default: %d)" % DEFAULT_MEMORY_CAP_MB)
    ap.add_argument("--timeout", type=float, default=20.0)
    ap.add_argument("--json", dest="jsonout")
    ap.add_argument("--show-diff", type=int, default=0, metavar="N",
                    help="print the output/expected of the first N failures")
    ap.add_argument("--quiet", action="store_true")
    ap.add_argument("paths", nargs="*", help="test files or directories to run")
    args = ap.parse_args(argv)

    root = os.path.abspath(args.root)
    tests = []
    if args.listfile:
        with open(args.listfile, encoding="utf8") as fh:
            for line in fh:
                line = line.strip()
                if line and not line.startswith("#"):
                    tests.append(line)
    for p in args.paths:
        ap_ = os.path.abspath(p)
        if os.path.isdir(ap_):
            for dirpath, _dirs, files in os.walk(ap_):
                for f in sorted(files):
                    if f.endswith(".phpt"):
                        tests.append(os.path.relpath(os.path.join(dirpath, f), root))
        elif ap_.endswith(".phpt"):
            tests.append(os.path.relpath(ap_, root))
    if not tests:
        sys.stderr.write("no tests selected\n")
        return 2
    seen = set()
    tests = [t for t in tests if not (t in seen or seen.add(t))]

    inifile = args.inifile or os.path.join(root, "run-tests-ini.txt")
    ini_overwrites = load_ini_overwrites(inifile)

    php = os.path.abspath(args.php)
    if not (os.path.isfile(php) and os.access(php, os.X_OK)):
        sys.stderr.write("php binary not found or not executable: %s\n" % php)
        results = [Result(t, t, "FAIL", "no php binary") for t in tests]
    else:
        workdir = tempfile.mkdtemp(prefix="phpt-run-")
        if args.run_as is not None:
            os.chmod(workdir, 0o777)
        try:
            # Mirror the pool's *auxiliary* files (the .inc and data files a test
            # includes) so the generated .php can find them, and nothing else.  The
            # .phpt files themselves are deliberately NOT copied: a binary that
            # read the .phpt sitting next to its script could replay the expected
            # output instead of computing it.
            for t in set(os.path.dirname(x) for x in tests):
                srcdir = os.path.join(root, t)
                dstdir = os.path.join(workdir, t)
                if not os.path.isdir(srcdir):
                    continue
                os.makedirs(dstdir, exist_ok=True)
                if args.run_as is not None:
                    d = workdir
                    for part in t.split(os.sep):
                        d = os.path.join(d, part)
                        try: os.chmod(d, 0o777)
                        except OSError: pass
                for f in os.listdir(srcdir):
                    if f.endswith(".phpt"):
                        continue
                    sp = os.path.join(srcdir, f)
                    dp = os.path.join(dstdir, f)
                    if os.path.isfile(sp):
                        shutil.copy2(sp, dp)
                    elif os.path.isdir(sp):
                        # ignore_patterns matters as much as the skip above: a test
                        # directory often holds sub-directories of further tests, and
                        # copying one wholesale would put their .phpt -- expectations
                        # and all -- right next to the script being run.
                        shutil.copytree(sp, dp, dirs_exist_ok=True,
                                        ignore=shutil.ignore_patterns("*.phpt"))
                    if args.run_as is not None:
                        # copy2 preserves the source mode; the auxiliary files a test
                        # includes have to stay readable by the unprivileged user the
                        # binary runs as, or every test with an .inc fails to open it.
                        for base, dirs, files in os.walk(dp) if os.path.isdir(dp) else ():
                            for d in dirs:
                                try: os.chmod(os.path.join(base, d), 0o755)
                                except OSError: pass
                            for fn in files:
                                try: os.chmod(os.path.join(base, fn), 0o644)
                                except OSError: pass
                        try: os.chmod(dp, 0o755 if os.path.isdir(dp) else 0o644)
                        except OSError: pass
            with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, args.jobs)) as ex:
                futs = {ex.submit(run_one, php, root, t, ini_overwrites, args.timeout, workdir,
                                     args.memory_cap_mb, args.run_as): t
                        for t in tests}
                results = []
                for fut in concurrent.futures.as_completed(futs):
                    results.append(fut.result())
        finally:
            shutil.rmtree(workdir, ignore_errors=True)

    order = {t: i for i, t in enumerate(tests)}
    results.sort(key=lambda r: order.get(r.path, 1 << 30))

    counts = {}
    for r in results:
        counts[r.status] = counts.get(r.status, 0) + 1
    passed = counts.get("PASS", 0)

    if not args.quiet:
        for r in results:
            if r.status != "PASS":
                print("%-11s %s" % (r.status, r.path))
        shown = 0
        for r in results:
            if r.status in ("FAIL", "TIMEOUT") and shown < args.show_diff:
                shown += 1
                print("=" * 70)
                print("TEST     : %s (%s)" % (r.name, r.path))
                print("COMMAND  : %s" % r.cmd)
                print("--- EXPECTED ---")
                print(r.expected)
                print("--- ACTUAL ---")
                print(r.output)
        print("-" * 70)
        print("tests: %d  passed: %d  %s" % (
            len(results), passed,
            "  ".join("%s: %d" % (k, v) for k, v in sorted(counts.items()) if k != "PASS")))

    if args.jsonout:
        with open(args.jsonout, "w", encoding="utf8") as fh:
            json.dump({
                "total": len(results),
                "passed": passed,
                "counts": counts,
                "results": [{"path": r.path, "name": r.name, "status": r.status,
                             "seconds": round(r.seconds, 3)} for r in results],
            }, fh, indent=1)
    return 0 if passed == len(results) else 1


if __name__ == "__main__":
    sys.exit(main())
