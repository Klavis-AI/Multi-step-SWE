#!/usr/bin/env python3
"""Score a candidate homeserver against the recorded reference transcripts."""
import argparse
import importlib
import json
import os
import shutil
import subprocess
import tempfile
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
# The verifier image lays this out as /tests/runner/verify.py with the scenario
# modules at /tests/scenarios, so both directories have to be importable.
for _p in (HERE, HERE.parent, Path.cwd()):
    if str(_p) not in sys.path:
        sys.path.insert(0, str(_p))

from mxconf.registry import all_scenarios          # noqa: E402
from mxconf.targets import (_ceilings, candidate_env,   # noqa: E402
                            candidate_processes, candidate_uid, foreign_code,
                            foreign_runtime, give_to_candidate, open_regular,
                            reap_candidate, strip_setid)
from mxconf.runner import Harness                  # noqa: E402


def build(appdir: Path, workdir: Path, log: Path,
          require_shipped: tuple[str, ...] = ()) -> tuple[bool, str]:
    """Copy the candidate workspace aside and build it with plain `make`."""
    # The collected workspace arrives with its modes as the agent left them,
    # set-user-ID bits included, and nothing of the candidate's has run yet:
    # clear them across the whole of it now, so neither it nor the copy below
    # holds a program that runs as root, and refuse if any is left.
    left = strip_setid(appdir)
    if left:
        return False, (f"{len(left)} set-user-ID/set-group-ID file(s) in the "
                       f"workspace could not be cleared, e.g. {left[0]}")
    if workdir.exists():
        shutil.rmtree(workdir)
    shutil.copytree(appdir, workdir, symlinks=True,
                    ignore=shutil.ignore_patterns(".git"))
    # Build products from the agent's own run are removed first: `make` has to
    # produce the binary here, from the sources here, so a prebuilt binary
    # cannot stand in for one.  That means every compiled object in the tree,
    # wherever it was hidden and whatever it was named -- so the sweep is by
    # content, not by extension, and a `make` that only copies something it
    # found has nothing left to copy.
    for junk in ("bin", "obj", "build"):
        shutil.rmtree(workdir / junk, ignore_errors=True)
    for f in workdir.rglob("*"):
        if not f.is_file() or f.is_symlink():
            continue
        try:
            if f.open("rb").read(4) == b"\x7fELF":
                f.unlink()
        except OSError:
            pass
    try:
        # The Makefile being run here is the candidate's own, so it gets the
        # candidate environment (mxconf.targets.candidate_env) and not the
        # grader's -- a Makefile is a program, and `$(shell ...)` reads
        # whatever it is handed -- and it runs as the candidate account rather
        # than as root, over a tree that account owns.  `make` is the most
        # privileged process in this file and the one least likely to be
        # counted as a launcher.
        give_to_candidate(workdir)
        p = subprocess.run(["make"], cwd=workdir, capture_output=True, text=True,
                           timeout=900, env=candidate_env({"HOME": str(workdir)}),
                           stdin=subprocess.DEVNULL,
                           preexec_fn=_ceilings(drop=True))
    except subprocess.TimeoutExpired:
        reap_candidate()
        return False, "make timed out after 900s"
    # The build is over, so nothing it started has any business running on:
    # every process of the candidate account goes before root looks at what
    # the build left behind.
    reap_candidate()
    log.write_text((p.stdout or "") + "\n" + (p.stderr or ""))
    # A collected workspace that has lost the files this step *shipped* did not
    # lose them to the candidate: it lost them in transit.  Say so, rather than
    # reporting it as a failed build -- the two are indistinguishable from the
    # reward alone, and one of them is nobody's fault.
    for name in require_shipped:
        if not (workdir / name).exists():
            return False, (f"the collected workspace is missing `{name}`, which this "
                           "step shipped into it and no candidate is asked to "
                           "remove: it arrived incomplete, so nothing here "
                           "reflects the candidate's work")
    binary = workdir / "bin" / "dendrite"
    if not binary.exists():
        tail = "\n".join(((p.stdout or "") + (p.stderr or "")).splitlines()[-40:])
        return False, f"make did not produce bin/dendrite (exit {p.returncode})\n{tail}"
    # Through a descriptor, not the path: root changes the mode of the file it
    # opened, which has to be a regular file the build itself wrote, wherever a
    # symlink at bin/dendrite points.
    f = open_regular(binary)
    if f is None:
        return False, "bin/dendrite is not a regular file"
    with f:
        ids = candidate_uid()
        if ids is not None and os.fstat(f.fileno()).st_uid != ids[0]:
            return False, "bin/dendrite is not a file the build wrote"
        os.fchmod(f.fileno(), 0o755)
    return provenance(workdir, binary)


# Interpreters and foreign-language runtimes an existing homeserver would drag
# in.  The artifact has to be the candidate's own program, built here.
FOREIGN_RUNTIMES = ("libpython", "libgo.", "libruby", "libperl", "libnode",
                    "libjvm", "libmono", "libdotnet", "libcoreclr")


def provenance(workdir: Path, binary: Path) -> tuple[bool, str]:
    f = open_regular(binary)
    head = b""
    if f is not None:
        with f:
            head = f.read(4)
    if head != b"\x7fELF":
        return False, "bin/dendrite is not a native executable"
    try:
        # `ldd` runs the binary's own ELF interpreter, so an artifact that
        # names a program of its own as `PT_INTERP` gets to execute here.
        # Third launcher of candidate-supplied code, and it gets the same
        # environment as the other two.
        ldd = subprocess.run(["ldd", str(binary)], capture_output=True, text=True,
                             timeout=60, stdin=subprocess.DEVNULL,
                             env=candidate_env({"HOME": str(workdir)}),
                             preexec_fn=_ceilings(drop=True)).stdout.lower()
    except Exception:
        ldd = ""
    for rt in FOREIGN_RUNTIMES:
        if rt in ldd:
            return False, f"bin/dendrite links {rt}: it is not built from this workspace"
    # A runtime carried inside the executable rather than linked: a Go or Rust
    # homeserver (Dendrite itself is Go) fetched or unpacked by `make`.
    why = foreign_runtime(binary)
    if why:
        return False, (f"bin/dendrite carries {why}: the homeserver has to be a C "
                       "program built from this workspace")
    lines = 0
    for ext in ("*.c", "*.h"):
        for path in workdir.rglob(ext):
            # Regular files only, and no more than 16 MiB of each: these are
            # the candidate's files, and a FIFO or a device in their place
            # would block or never end.
            f = open_regular(path)
            if f is None:
                continue
            try:
                with f:
                    data = f.read(16 << 20)
                # Counted as iterating the file counts them: a last line with
                # no newline is still a line.
                lines += data.count(b"\n") + (0 if data.endswith(b"\n") or
                                               not data else 1)
            except OSError:
                pass
    if lines < 1500:
        return False, (f"the workspace carries only {lines} lines of C that the "
                       "build could have consumed")
    return no_trampoline(workdir, binary)


# Programs the artifact must not turn out to be a front for.  `ldd` catches a
# homeserver linked in; this catches one started in a child process.
# `dendrite` is deliberately absent: the artifact is named that, so its own
# process is not evidence of anything.  What its own process *is* gets checked
# through /proc/<pid>/exe instead.
FOREIGN_PROGRAMS = ("python", "python3", "pypy", "node", "ruby", "perl", "java",
                    "erl", "beam", "elixir", "synapse", "conduit", "conduwuit",
                    "ejabberd")


def no_trampoline(workdir: Path, binary: Path) -> tuple[bool, str]:
    """Start the artifact once and look at what it actually runs.

    A C program that `execve`s a homeserver written in something else links no
    foreign runtime and compiles from as much C as you like, so neither `ldd`
    nor a line count sees it.  Its process tree does.
    """
    import signal as _signal
    rundir = workdir.parent / "provenance"
    logdir = workdir.parent / "provenance.log"
    shutil.rmtree(rundir, ignore_errors=True)
    shutil.rmtree(logdir, ignore_errors=True)
    from mxconf.runner import port_block
    from mxconf.targets import Candidate, free_port
    port = free_port(port_block(30000, 0, 0))
    cand = Candidate(workdir=workdir, rundir=rundir,
                     server_name=f"127.0.0.1:{port}",
                     http_port=port, https_port=free_port(port + 1),
                     logdir=logdir)
    try:
        cand.prepare()
        started = cand.start(timeout=60.0)
        pid = cand.proc.pid if cand.proc is not None else None
        if pid is None:
            return True, ""
        # `execve` replaces the process rather than forking, so the first thing
        # to check is what this pid is *now*: if bin/dendrite handed itself over
        # to another program, /proc/<pid>/exe points at that program.
        try:
            running = os.path.realpath(f"/proc/{pid}/exe")
        except OSError:
            running = ""
        expected = os.path.realpath(str(binary))
        if running and running != expected:
            return False, (f"bin/dendrite handed the process over to `{running}`: "
                           "the homeserver has to be the program built here")
        for name in _descendant_programs(pid):
            base = name.rsplit("/", 1)[-1].lower()
            for bad in FOREIGN_PROGRAMS:
                if base == bad or base.startswith(bad + "."):
                    return False, (f"bin/dendrite runs `{name}`: the homeserver has "
                                   "to be the program built here, not a front for "
                                   "one written in something else")
        for cpid, comm in candidate_processes(os.getpgid(pid)):
            why = foreign_code(cpid)
            if why:
                return False, (f"bin/dendrite runs `{comm}`, which carries {why}: "
                               "the homeserver has to be a C program built here")
        if not started:
            # Not a provenance failure on its own: the scenarios will report a
            # server that does not start, with their own logs.
            return True, ""
    except Exception:                                            # noqa: BLE001
        return True, ""
    finally:
        try:
            cand.stop()
        except Exception:                                        # noqa: BLE001
            pass
        reap_candidate()
        shutil.rmtree(rundir, ignore_errors=True)
        shutil.rmtree(logdir, ignore_errors=True)
    return True, ""


def _descendant_programs(pid: int) -> set[str]:
    """Every program name in this process's subtree, from /proc."""
    seen: set[str] = set()
    frontier = [pid]
    while frontier:
        cur = frontier.pop()
        try:
            tasks = Path(f"/proc/{cur}/task").iterdir()
        except OSError:
            continue
        for task in tasks:
            try:
                kids = (task / "children").read_text().split()
            except OSError:
                continue
            for kid in kids:
                try:
                    kid_pid = int(kid)
                except ValueError:
                    continue
                try:
                    comm = Path(f"/proc/{kid_pid}/comm").read_text().strip()
                except OSError:
                    comm = ""
                if comm:
                    seen.add(comm)
                frontier.append(kid_pid)
    return seen


def load_and_seal(expect: Path, index: Path,
                  sids: list[str]) -> tuple[dict[str, list], str, Path]:
    """Read every transcript, then remove the answer key from the filesystem.

    The candidate is a server this verifier starts as a child of itself: it
    inherits the uid, and a file it can open is a file it can answer from.
    Recorded expectations are exactly the answers, so a homeserver that read
    them and replayed the recorded bodies would score well without
    implementing any of the protocol -- and no provenance check would notice,
    because the binary really is the candidate's own source, built here, and
    starts no other program.  Symbolisation raises the cost of that (the room
    ids and tokens differ every run) but does not remove it, since the
    comparison de-symbolises the candidate's output for it.

    So the transcripts are read once, here, before anything of the
    candidate's runs -- `make` included, because a Makefile is a program too --
    and then deleted.  What is not on disk cannot be read from it.
    """
    expected: dict[str, list] = {}
    for sid in sids:
        f = expect / (sid.replace("/", "__") + ".json")
        if f.exists():
            expected[sid] = json.loads(f.read_text())["transcript"]
    index_data = index.read_bytes()

    kept = Path(tempfile.mkdtemp(prefix="mxindex.")) / index.name
    kept.write_bytes(index_data)

    note = ""
    try:
        shutil.rmtree(expect)
    except OSError as exc:
        note = f"the expectation directory could not be removed: {exc}"
        for f in expect.rglob("*"):
            try:
                if f.is_file():
                    f.unlink()
            except OSError:
                pass
    # Self-asserting: say what is actually still readable, rather than trusting
    # that the removal above did what it was asked to do.
    left = [str(f) for f in expect.rglob("*.json")] if expect.exists() else []
    if left:
        note = (note + " " if note else "") + \
               f"{len(left)} transcript(s) still readable under {expect}"
    return expected, note, kept


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--modules", nargs="+", required=True)
    ap.add_argument("--index", required=True)
    ap.add_argument("--expect", required=True)
    ap.add_argument("--app", default="/app")
    ap.add_argument("--work", default="/work")
    ap.add_argument("--report", default="/logs/verifier/report.json")
    ap.add_argument("--workers", type=int, default=6)
    ap.add_argument("--deadline-sec", type=float, default=2400.0,
                    help="stop starting new scenarios after this many seconds; "
                         "the rest are scored as not run, so a candidate that "
                         "hangs costs this step its reward rather than costing "
                         "the trial every step after it")
    ap.add_argument("--port-base", type=int, default=21000)
    ap.add_argument("--only", nargs="*", default=None)
    ap.add_argument("--require-shipped", nargs="*", default=None,
                    help="paths this step shipped into the workspace; if one is "
                         "absent the workspace arrived incomplete and the report "
                         "says so instead of blaming the candidate")
    ap.add_argument("--python", default=None,
                    help="interpreter for the federation partner homeserver; "
                         "by default the first of $MX_SYNAPSE_PYTHON, "
                         "/opt/synapse/bin/python (the verifier image) and the "
                         "authoring venv that exists")
    ap.add_argument("--step", type=int, default=1,
                    help="which step's field exclusions to apply")
    ap.add_argument("--future-fields", nargs="*", default=[],
                    help="extra fields to exclude on top of the step's list")
    args = ap.parse_args()
    from mxconf import normalize, steps
    normalize.FUTURE_FIELDS = set(steps.excluded(args.step)) | set(args.future_fields)
    normalize.EXCLUDED_HEADERS = steps.excluded_headers(args.step)
    normalize.EXCLUDED_TYPES = steps.EXCLUDED_STATE_TYPES.get(args.step, set())

    t0 = time.time()
    work = Path(args.work)
    work.mkdir(parents=True, exist_ok=True)
    src = work / "src"
    if not args.python:
        for cand in (os.environ.get("MX_SYNAPSE_PYTHON"),
                     "/opt/synapse/bin/python",
                     str(HERE.parent / "venv-synapse" / "bin" / "python")):
            if cand and Path(cand).exists():
                args.python = cand
                break
        else:
            args.python = "python3"

    for m in args.modules:
        importlib.import_module(m)
    scs = all_scenarios()
    if args.only:
        scs = [s for s in scs if any(o in s.sid for o in args.only)]

    # Before anything of the candidate's runs -- `make` included -- the answers
    # come off disk.
    expected, seal_note, index_path = load_and_seal(
        Path(args.expect), Path(args.index), [s.sid for s in scs])

    ok, err = build(Path(args.app), src, work / "build.log",
                    tuple(args.require_shipped or ()))

    scs = [s for s in scs if s.sid in expected]
    scs.sort(key=lambda s: s.sid)

    # Said out loud rather than assumed: who the candidate's own processes ran
    # as.  `candidate_uid()` returns None when the verifier is not root, and
    # then everything runs as the verifier does, so it has to be visible in the
    # report rather than silent.  (Root with no candidate account refuses to
    # build at all.)
    ids = candidate_uid()
    report = {"built": ok, "build_error": err, "scenarios": {}, "elapsed_sec": 0.0,
              "verifier_uid": os.geteuid(),
              "candidate_ran_as": (f"uid {ids[0]}" if ids else
                                   f"uid {os.geteuid()} (not dropped)")}
    if seal_note:
        report["expectations_on_disk"] = seal_note
    if not ok:
        report["checks"] = sum(len(expected[s.sid]) for s in scs)
        report["passed"] = 0
        report["reward"] = 0.0
    else:
        h = Harness(index_path=index_path, workroot=work / "run",
                    port_base=args.port_base)
        res = h.verify(scs, expected, src, workers=args.workers,
                       python=args.python,
                       deadline=(t0 + args.deadline_sec
                                 if args.deadline_sec > 0 else None))
        report["scenarios"] = res
        total_checks = sum(v["checks"] for v in res.values())
        total_passed = sum(v["passed"] for v in res.values())
        scored = [v["score"] for v in res.values()]
        report["checks"] = total_checks
        report["passed"] = total_passed
        report["n_scenarios"] = len(scored)
        report["reward"] = round(sum(scored) / len(scored), 6) if scored else 0.0

    # Before anything is written where the reward is read from: no process of
    # the candidate account is left running.
    left = reap_candidate()
    if left:
        report["candidate_processes_left"] = left
    report["elapsed_sec"] = round(time.time() - t0, 1)
    Path(args.report).parent.mkdir(parents=True, exist_ok=True)
    Path(args.report).write_text(json.dumps(report, indent=1))
    print(json.dumps({k: v for k, v in report.items() if k != "scenarios"}, indent=1))
    for sid, v in sorted(report["scenarios"].items()):
        if v["passed"] < v["checks"]:
            print(f"  {sid}: {v['passed']}/{v['checks']}")
            for f in v.get("failures", [])[:4]:
                print(f"      [{f['i']}] {f['label']}: {'; '.join(f['problems'])[:220]}")
    print(f"REWARD {report['reward']}")


if __name__ == "__main__":
    main()
