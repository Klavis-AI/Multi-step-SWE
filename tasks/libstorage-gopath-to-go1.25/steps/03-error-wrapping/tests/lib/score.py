"""Score one step of the migration and write the breakdown to file descriptor 3.

The reward is a product of two factors:

    reward = T1 * T2,   T1 = F2,   T2 = F1 * R

so a tree that has not done this step's migration (F2 = 0) scores 0 however
well it behaves, and a tree that has done it scores only as much as it still
behaves and still holds what earlier steps established.

F1, **behavior equivalence** (part of T2). The recorded wire suite replayed against a
    binary the verifier builds from the candidate's tree (75% of F1), plus the
    repository's own test suite (25%). Both come from the verifier's own
    pristine copies of the harness and the expectations; the candidate never
    sees either. The untouched baseline scores 0 here at step 1 because the
    target toolchain cannot build a GOPATH tree outside GOPATH, which is the
    step's whole subject.

F2, **migrated-ness** (T1). The gates this step introduces, each graded
    against its baseline count so a partial migration earns partial credit.
    These are what the previous step's finished tree fails completely.

R, **anti-regression** (part of T2, 0 to 1). The weighted mean of the gates
    earlier steps established, plus the anti-weakening gates: suppression
    comments, skipped tests, and the size of the test corpus.

Nothing here reads a file the candidate could have written to decide what to
run: the suite, the gate specification and the tool binaries all live in the
verifier image.

This process runs at the candidate's uid, and so does the candidate's server,
so it never sees the recorded answers: `test.sh` makes `expect/` root-only
before any candidate code runs. The replay here only collects the server's
normalized answers; `judge.py`, run as root, compares them, decides the one
transport retry, and computes the reward with `combine` below. The breakdown
goes to file descriptor 3, which `test.sh` opens on a root-owned file, rather
than to a path the candidate's processes could replace.

For the same reason the census and the gates do not read /app, which is the
candidate's uid's to change while its server and its tests run: they read the
root-owned copy `test.sh` took before any candidate code ran, after every
other process at this uid has been killed (`enter_gates`).
"""
import json
import os
import re
import sys
import tempfile
import time

import gates
import toolenv

LIB = os.path.dirname(os.path.abspath(__file__))

# Within the behavior family.
W_RECORDED = 0.75
W_OWN_TESTS = 0.25


class Context(object):
    """Everything the gates need, computed once."""

    def __init__(self, root, env, binary, spec):
        self.root = root
        self.env = env
        self.binary = binary
        self.spec = spec
        self.deps = []
        self.own = []
        self.repo_dirs = []
        self.own_dirs = []
        self.repo_files = []
        self.own_files = []
        self.census_repo = {}
        self.census_own = {}

    def tool(self, name):
        return os.path.join(toolenv.TOOLS, name)


def tree_census(log):
    """Count the files in /app, so an empty tree is distinguishable from one
    that merely does not build.

    Both produce `packages.own == 0`. The untouched 2017 tree is not a module,
    so `go list ./...` enumerates nothing and every gate reports that -- which
    is the correct answer for the nop, and the whole subject of this step. An
    artifact round trip that delivers no source at all produces the identical
    zero for an entirely different reason, and from outside the container the
    only thing that separates the two is whether there were any files to grade.
    This is diagnostic only and does not feed the reward.
    """
    files = go_files = 0
    for dirpath, dirnames, filenames in os.walk(toolenv.ROOT):
        dirnames[:] = [d for d in dirnames if d != ".git"]
        for name in filenames:
            files += 1
            if name.endswith(".go"):
                go_files += 1
    log["tree"] = {"files": files, "go_files": go_files}


def build(env, log):
    """Build the server the recorded suite is replayed against."""
    started = time.time()
    out = toolenv.run(
        ["go", "build", "-o", "/tmp/lss", "./cli/lss/lss-linux"],
        env=env, timeout=900)
    log["build"] = {"code": out.code, "seconds": round(time.time() - started, 1),
                    "stderr": out.stderr[-4000:]}
    return "/tmp/lss" if out.code == 0 and os.path.exists("/tmp/lss") else None


def combine(recorded, own, migrated, regression):
    """The reward from its four inputs. `judge.py` calls this, as root.

    reward = T1 * T2 with T1 = migrated and T2 = behavior * regression.
    Returns (reward, behavior, T2).
    """
    behavior = W_RECORDED * recorded + W_OWN_TESTS * own
    preservation = behavior * regression
    reward = migrated * preservation
    return max(0.0, min(1.0, reward)), behavior, preservation


def collect_answers(binary, ids=None, timeout=1500):
    """Run the recorded suite's scenarios and return the server's answers.

    The answers come back on the replay's stdout, a pipe only this process
    holds, and are compared with the recording by `judge.py` as root.
    """
    if binary is None:
        return None, None
    argv = [sys.executable, "-s", "-E", os.path.join(LIB, "replay.py"),
            os.path.join(LIB, "suite.json"), "-",
            binary, toolenv.ROOT, "--jobs", "6", "--record", "-"]
    if ids:
        argv += ["--ids", ",".join(ids)]
    out = toolenv.run(argv, env=toolenv.base_env(), cwd="/tmp",
                      timeout=timeout)
    lines = out.stdout.strip().splitlines()
    answers = None
    if lines:
        try:
            answers = json.loads(lines[-1])
        except ValueError:
            answers = None
    if not isinstance(answers, dict):
        answers = None
    return answers, out


def replay(binary, log):
    """F1a: collect the server's answers to the recorded wire suite.

    Scored by `judge.py` as root, not here.
    """
    if binary is None:
        log["recorded"] = {"score": 0.0, "reason": "no binary"}
        log["replay"] = {"answers": None}
        return
    started = time.time()
    answers, out = collect_answers(binary)
    log["recorded"] = {
        "seconds": round(time.time() - started, 1),
        "timedout": out.timedout,
        "code": out.code,
        "scenarios": len(answers or {}),
        "stderr": out.stderr[-2000:],
    }
    log["replay"] = {"answers": answers}


_PKG_RESULT = re.compile(r"^(ok|FAIL|---|\?)\s+(\S+)")


def _own_test_results(text):
    """Package results out of `go test` output: (passed, failed, lines, pkgs).

    `pkgs` is the import paths named on a `FAIL <pkg>` line, which is what a
    second attempt re-runs.
    """
    passed = failed = 0
    lines, pkgs = [], []
    for line in text.splitlines():
        match = _PKG_RESULT.match(line)
        if not match:
            continue
        kind = match.group(1)
        if kind == "ok":
            passed += 1
        elif kind in ("FAIL", "---"):
            failed += 1
            lines.append(line)
            if kind == "FAIL":
                pkgs.append(match.group(2))
    return passed, failed, lines, pkgs


def own_tests(env, log):
    """F1b: the repository's own suite, run from the candidate's tree.

    Running these from a pristine copy is not possible for this task: the
    migration changes the signatures the tests call, so a frozen copy would stop
    compiling at step 2 for reasons that have nothing to do with the candidate.
    The tests are therefore run as the candidate maintains them, and the
    anti-weakening gates in R are what stop the corpus from shrinking.

    A package that fails is run once more, on its own, and is credited if that
    second run passes -- the same rule `probe_run`'s `retry` applies from step 6
    on. The inherited 2017 suite is not deterministic under heavy host load:
    `drivers/storage/vfs/tests` can crash on the reference tree (with no
    `--- FAIL` line) and pass on the next run. A package that is broken rather
    than unlucky fails both runs and is still failed. No second run is spent
    when the first one hit the per-package timeout: a deadlocked tree pays the
    five minutes once.
    """
    started = time.time()
    # 5m per package. The slowest package in this repository is the vfs
    # integration suite at 58s, so the bound has better than 5x headroom over
    # a healthy tree and can never truncate one. It is kept low because the
    # shape of a half-finished shutdown here is a DEADLOCK -- the server's
    # close handshake waits on a goroutine that has already left its select --
    # and such a tree should pay only 5 minutes for the goroutine dump that
    # shows it.
    out = toolenv.run(
        ["go", "test", "-vet=off", "-count=1", "-timeout", "5m", "./..."],
        env=env, timeout=600)
    text = out.stdout + "\n" + out.stderr
    passed, failed, lines, pkgs = _own_test_results(text)
    retry = None
    if pkgs and not out.timedout and "panic: test timed out" not in text:
        first = {"failures": lines[:40], "stdout": out.stdout[-4000:]}
        again = toolenv.run(
            ["go", "test", "-vet=off", "-count=1", "-timeout", "5m"]
            + sorted(set(pkgs)), env=env, timeout=600)
        p2, f2, l2, k2 = _own_test_results(again.stdout + "\n" + again.stderr)
        # Every package the first run named is either credited by the second
        # run's `ok` or still failed; a second run that reports nothing for a
        # package (killed, or crashed before printing) leaves it failed.
        still = max(f2, len(set(pkgs)) - p2)
        retry = dict(first, recovered=p2, still_failed=still,
                     code=again.code, timedout=again.timedout)
        passed, failed, lines = passed + p2, still, l2
    total = passed + failed
    score = (float(passed) / total) if total else 0.0
    log["own_tests"] = {
        "score": round(score, 6), "passed": passed, "total": total,
        "seconds": round(time.time() - started, 1), "code": out.code,
        "timedout": out.timedout, "failures": lines[:40],
        "stderr": out.stderr[-2000:], "retry": retry,
    }
    return score


def go_source_tree(root):
    """Every directory holding Go source, and every Go file, under `root`.

    Read from the filesystem rather than from `go list`, because `go list` only
    enumerates what the *current* platform and tag set can build. This tree has
    four files the toolchain does not enumerate on linux -- a package behind
    `// +build mock`, a darwin driver, two platform mains -- and a scope taken
    from `go list` would leave them permanently ungated, which is exactly the
    corner a half-done migration rots in. The walk has no exclusion list: every
    directory with a `.go` file in it is in scope, vendored source included.

    Both halves are returned because the two consumers want different things:
    the census reads a directory at a time, and `gofmt` has to be handed files.
    Handing `gofmt` a directory makes it recurse, which would silently put the
    whole tree back inside a scope meant to exclude part of it.
    """
    dirs, files = set(), []
    for dirpath, _, names in os.walk(root):
        found = [n for n in names if n.endswith(".go")]
        if not found:
            continue
        dirs.add(dirpath)
        files.extend(os.path.join(dirpath, n) for n in sorted(found))
    return sorted(dirs), sorted(files)


def first_party(root, paths):
    """The subset of `paths` that is not under the toolchain's `vendor` tree.

    `vendor` is the one directory name both GOPATH mode and module mode reserve
    -- `./...` never matches inside it -- so splitting on it follows the
    language's own rule. If the candidate removes
    vendoring, the two scopes coincide and nothing here applies.
    """
    out = []
    for path in paths:
        rel = os.path.relpath(path, root)
        if rel == ".":
            out.append(path)
            continue
        if "vendor" in rel.split(os.sep):
            continue
        out.append(path)
    return out


def census(ctx, log):
    """Run the AST census over two scopes: the repository, and its own code.

    `-root` is the repository, so the census's per-file maps are keyed by the
    path inside it rather than by wherever the scorer unpacked the tree. A gate
    specification can then name a file and mean the same file every run.
    """
    repo_dirs, repo_files = go_source_tree(ctx.root)
    own_dirs = first_party(ctx.root, repo_dirs)
    ctx.repo_dirs = repo_dirs
    ctx.own_dirs = own_dirs
    ctx.repo_files = repo_files
    ctx.own_files = first_party(ctx.root, repo_files)
    for name, dirs in (("census_repo", repo_dirs), ("census_own", own_dirs)):
        if not dirs:
            setattr(ctx, name, {})
            continue
        out = toolenv.run([ctx.tool("astcount"), "-root", ctx.root] + dirs,
                          env=ctx.env, timeout=300)
        try:
            setattr(ctx, name, json.loads(out.stdout))
        except ValueError:
            setattr(ctx, name, {})
            log.setdefault("census_errors", []).append(
                {"scope": name, "stderr": out.stderr[-1000:]})
    log["census"] = {"repo_dirs": len(repo_dirs), "own_dirs": len(own_dirs),
                     "repo_files": len(ctx.repo_files),
                     "own_files": len(ctx.own_files),
                     "own": ctx.census_own}


# Gate kinds that compile and run the candidate's code. They run after every
# gate that only reads the tree, and each is followed by a reap, so nothing
# they start is alive while another gate is being measured.
RUNS_CANDIDATE_CODE = frozenset(["race", "probe"])


def run_families(ctx, families, log):
    """Run every family's gate specifications; return each one's weighted mean.

    A tree the target toolchain cannot enumerate -- no module, or a module that
    does not load -- scores zero on every gate rather than on none of them.
    Without this a count-based gate would read an empty census and mistake "the
    toolchain found nothing to look at" for "there is nothing left to fix".

    The gates run in two passes -- the ones that only read the tree first, then
    the ones that run candidate code -- and are reported, weighted and averaged
    in the specification's own order, so the order changes nothing but which
    processes can be alive while a tool is measuring.
    """
    jobs = [(label, i, spec) for label, specs in families
            for i, spec in enumerate(specs)]
    order = ([j for j in jobs if j[2]["kind"] not in RUNS_CANDIDATE_CODE]
             + [j for j in jobs if j[2]["kind"] in RUNS_CANDIDATE_CODE])
    results, reaps = {}, []
    for label, i, spec in order:
        if not ctx.own:
            result = gates.Result(spec.get("id") or spec["kind"], 0.0,
                                  {"error": "go list ./... enumerated no "
                                            "packages; the tree is not a "
                                            "loadable module"})
        else:
            result = gates.run_gate(ctx, spec)
            if spec["kind"] in RUNS_CANDIDATE_CODE:
                reaped = toolenv.reap_strays()
                if reaped.get("found") or reaped.get("left"):
                    reaps.append(dict(reaped, after=result.id))
        results[(label, i)] = result
    if reaps:
        log["reaped_after_gates"] = reaps[:40]
    means = {}
    for label, specs in families:
        out, total, weighted = [], 0.0, 0.0
        for i, spec in enumerate(specs):
            result = results[(label, i)]
            weight = float(spec.get("weight", 1))
            out.append(dict(result.as_dict(), weight=weight))
            total += weight
            weighted += weight * result.score
        log[label] = out
        means[label] = (weighted / total) if total else 1.0
    return means


def enter_gates(log):
    """Leave the candidate's processes and its live tree behind.

    Everything before this point ran the candidate's code at this uid: its
    server during the replay, its tests. Every other process at the uid is
    killed here, and the gates then read root's copy of the tree and root's
    copy of the module cache (toolenv.GATED_*), with build and analysis caches
    made fresh in a directory created after the reap. Returns the gates'
    environment.
    """
    log["reaped_before_gates"] = toolenv.reap_strays()
    scratch = tempfile.mkdtemp(prefix="gates-")
    toolenv.ROOT = toolenv.GATED_ROOT
    log["gated"] = {"root": toolenv.GATED_ROOT,
                    "present": os.path.isdir(toolenv.GATED_ROOT),
                    "modcache": toolenv.GATED_MODCACHE}
    return toolenv.gate_env(scratch)


def main():
    spec_path = os.path.join(LIB, "gatespec.json")
    spec = _read_json(spec_path)
    log = {"step": spec.get("step"), "started": int(time.time())}
    env = toolenv.base_env()

    tree_census(log)
    binary = build(env, log)
    replay(binary, log)
    repo_own = own_tests(env, log)

    # From here on nothing reads /app: the census and every gate measure the
    # tree as it was handed in (enter_gates).
    genv = enter_gates(log)
    ctx = Context(toolenv.ROOT, genv, binary, spec)
    ctx.deps = gates.loadable_packages(gates.go_list_deps(genv))
    ctx.own = gates.loadable_packages(
        gates.own_packages(gates.go_list_packages(genv)))
    log["packages"] = {"deps": len(ctx.deps), "own": len(ctx.own)}
    census(ctx, log)

    means = run_families(ctx, [("gates", spec["gates"]),
                               ("regression", spec.get("regression") or [])],
                         log)
    migrated, regression = means["gates"], means["regression"]

    # Everything but the recorded suite's score, which judge.py adds as root.
    log["inputs"] = {"own_tests": repo_own, "migrated": migrated,
                     "regression": regression}
    log["finished"] = int(time.time())

    _emit(log)
    print("own %.4f  migrated %.4f  regression %.4f  (recorded suite: "
          "scored by judge.py)" % (repo_own, migrated, regression))
    return 0


def retry(ids):
    """The one transport retry `judge.py` asked for: answers for `ids` only."""
    binary = "/tmp/lss" if os.path.exists("/tmp/lss") else None
    answers, _ = collect_answers(binary, [i for i in ids.split(",") if i])
    _emit({"answers": answers})
    return 0


def _emit(obj):
    """Write the result to fd 3, which test.sh opened on a root-owned file."""
    with os.fdopen(3, "w") as fh:
        json.dump(obj, fh, indent=2, sort_keys=True)
        fh.write("\n")


def _read_json(path):
    try:
        with open(path) as fh:
            return json.load(fh)
    except (IOError, OSError, ValueError):
        return None


if __name__ == "__main__":
    # Not dumpable: this process shares its uid with the
    # candidate's, so on a host whose yama ptrace_scope is 0 one of those could
    # otherwise attach to it or open its /proc/<pid>/mem. exec resets the flag,
    # so nothing this process starts is affected. PR_SET_DUMPABLE is 4.
    try:
        import ctypes
        ctypes.CDLL(None).prctl(4, 0, 0, 0, 0)
    except Exception:                               # never fatal
        pass
    if len(sys.argv) == 3 and sys.argv[1] == "--retry":
        sys.exit(retry(sys.argv[2]))
    sys.exit(main())
