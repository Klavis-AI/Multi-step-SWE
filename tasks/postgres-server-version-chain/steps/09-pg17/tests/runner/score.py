#!/usr/bin/env python3
"""Verifier driver: build the candidate, start it, run every scored component
against it, diff against the recorded expectations, write reward.json.

Each component is scored independently: one that crashes costs only its own
weight, so a partly working server still earns partial credit."""
import hashlib, json, os, pathlib, subprocess, sys, tempfile, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import serverctl

HERE = pathlib.Path(os.path.dirname(os.path.abspath(__file__)))

# The whole component phase has to finish inside the step's verifier timeout,
# with room left for the verifier image build and the artifact round trip.  Each
# component is capped individually, and they also share this deadline: a server
# that hangs costs its own component the remaining time and the ones after it a
# score of zero, rather than costing the step its reward by running past the
# timeout with nothing written.
COMPONENT_BUDGET_S = 1800


def jdump(p, obj):
    pathlib.Path(p).write_text(json.dumps(obj, indent=1, sort_keys=True, default=str))


def expected_root(corpus):
    """The directory of recorded answers for THIS milestone.

    The rung is in the directory NAME, and that is the point.  Docker's
    build-context sync treats a file as unchanged when (relpath, size, mtime)
    all match, so two steps whose expectation files share a name, a byte count
    and a whole second can be handed the SAME bytes -- one milestone graded
    against a neighbour's answers, silently, with a plausible score.  This
    package had seventeen such (relpath, size) pairs; `r01_guc_version.out` is
    304 bytes in eight of the ten steps with eight different answers, and it is
    the version probe.  A shared relpath is the only thing that makes the
    substitution possible at all, so the consumer derives the path from its own
    rung instead of hardcoding a name every step shares.

    `manifest.json` is the anchor because it cannot itself be substituted: its
    size differs in all ten steps.
    """
    c = pathlib.Path(corpus)
    rung = int(json.loads((c / "manifest.json").read_text())["rung"])
    return c / ("expected-r%02d" % rung)


def probe_workspace_access(app, work):
    """Can the user the server runs as still read its own workspace and write
    where it has to?  Checked as that uid, not assumed."""
    if not serverctl.server_user_available():
        return "server runs as the verifier's own user: nothing to check"
    def as_server(*argv):
        return subprocess.run(["setpriv", "--reuid", str(serverctl.SERVER_UID),
                               "--regid", str(serverctl.SERVER_GID), "--clear-groups"] + list(argv),
                              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0
    app = pathlib.Path(app); work = pathlib.Path(work)
    bad = []
    if not as_server("test", "-x", str(app / "bin/postgres")):
        bad.append("cannot execute bin/postgres")
    if not as_server("test", "-x", str(app / "bin/initdb")):
        bad.append("cannot execute bin/initdb")
    if not as_server("test", "-r", str(app / "Makefile")):
        bad.append("cannot read the Makefile")
    if not as_server("sh", "-c", "touch %s/.probe && rm -f %s/.probe" % (work, work)):
        bad.append("cannot write in the work directory")
    return "ok" if not bad else "PROBLEM: " + "; ".join(bad)


def probe_expectation_access(corpus):
    """Can the user the candidate's server runs as read the scored answers?"""
    import glob
    c = pathlib.Path(corpus)
    e = expected_root(c)
    targets = sorted(glob.glob(str(e / "sql" / "*.out")))[:1]
    targets += sorted(glob.glob(str(c / "cases" / "*.sql")))[:1]
    targets += [str(c / "manifest.json"), str(e / "protocol.json"),
                str(e / "isolation.json")]
    if not serverctl.server_user_available():
        return "server runs as the verifier's own user (uid %d): expectations ARE readable" % os.geteuid()
    readable = []
    for t in targets:
        r = subprocess.run(["setpriv", "--reuid", str(serverctl.SERVER_UID),
                            "--regid", str(serverctl.SERVER_GID), "--clear-groups",
                            "cat", t], stdout=subprocess.DEVNULL,
                           stderr=subprocess.DEVNULL)
        if r.returncode == 0:
            readable.append(t)
    # a directory listing is an index even when the files are not readable
    for d2 in (str(c), str(e / "sql"), str(c / "cases"), "/tests"):
        r = subprocess.run(["setpriv", "--reuid", str(serverctl.SERVER_UID),
                            "--regid", str(serverctl.SERVER_GID), "--clear-groups",
                            "ls", d2], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        if r.returncode == 0:
            readable.append(d2 + "/ (listing)")
    if readable:
        return "READABLE by uid %d: %s" % (serverctl.SERVER_UID, readable)
    return "no: uid %d cannot read or list any of the %d probed paths" % (serverctl.SERVER_UID, len(targets) + 4)


def tests_fingerprint(*roots):
    """A digest of everything the verifier is made of.

    The build and the server both run unprivileged and cannot write here, so
    this is a backstop rather than the defence -- but it needs no enumeration of
    the ways in, which is exactly what makes it worth having: it catches a path
    nobody thought to close.  A corpus you can overwrite is not a commitment.
    """
    h = hashlib.sha256()
    n = 0
    seen = set()
    for root in roots:
        r = pathlib.Path(root).resolve()
        if not r.exists() or r in seen:
            continue
        seen.add(r)
        for f in sorted(q for q in r.rglob("*") if q.is_file()):
            if f.suffix == ".pyc" or "__pycache__" in f.parts:
                continue
            h.update(str(f).encode())
            h.update(f.read_bytes())
            n += 1
    return (h.hexdigest(), n) if n else (None, 0)


def probe_reward_writable(out):
    """Can the server's user write where the reward lives?

    Probing the overwrite of `reward.json` alone can pass for the accidental
    reason that the file already exists with restrictive modes, so this also
    probes CREATION of a new name -- that is what settles the directory rather
    than the file.  A directory the candidate cannot write is a race it cannot
    enter, which is the same boundary that keeps it out of the expectations.
    """
    if not serverctl.server_user_available():
        return "server runs as the verifier's own user: the reward IS writable"
    out = pathlib.Path(out)
    writable = []
    for t in (out / "reward.json", out / "PROBE_CREATE", out / "score.log",
              pathlib.Path("/tests") / "test.sh"):
        r = subprocess.run(["setpriv", "--reuid", str(serverctl.SERVER_UID),
                            "--regid", str(serverctl.SERVER_GID), "--clear-groups",
                            "sh", "-c", "echo probe >> '%s'" % t],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        if r.returncode == 0:
            writable.append(str(t))
    try:
        (out / "PROBE_CREATE").unlink()
    except OSError:
        pass
    if writable:
        return "WRITABLE by uid %d: %s" % (serverctl.SERVER_UID, writable)
    return "no: uid %d can neither overwrite nor create there" % serverctl.SERVER_UID


def bringup_diagnostics(app, work, out):
    """Why a server did not come up is an environment question, and the
    environment is gone by the time anyone reads report.json.  A verifier that
    can fail environmentally has to keep the artefact that names the failure:
    who owns the tree, what the modes are, whether there is any space left, and
    what umask the pod handed us -- that last one differs between a local
    `docker run` and a pod, which is exactly how this class of bug hides."""
    import pwd
    app, work, out = pathlib.Path(app), pathlib.Path(work), pathlib.Path(out)
    lines = []
    def sh(label, argv):
        lines.append("$ " + " ".join(argv) + "   # " + label)
        try:
            r = subprocess.run(argv, capture_output=True, text=True, timeout=30)
            lines.append(r.stdout.rstrip() or "(no output)")
            if r.stderr.strip():
                lines.append("stderr: " + r.stderr.strip()[:800])
        except Exception as exc:
            lines.append("(%s: %s)" % (type(exc).__name__, exc))
        lines.append("")
    um = os.umask(0o22); os.umask(um)
    lines.append("umask as the verifier inherited it: %04o" % um)
    lines.append("verifier euid=%d egid=%d" % (os.geteuid(), os.getegid()))
    try:
        ent = pwd.getpwuid(serverctl.SERVER_UID)
        lines.append("server user: uid=%d name=%s home=%s shell=%s"
                     % (serverctl.SERVER_UID, ent.pw_name, ent.pw_dir, ent.pw_shell))
    except KeyError:
        lines.append("server user: uid=%d has no passwd entry (expected; it is "
                     "created by the image)" % serverctl.SERVER_UID)
    lines.append("")
    for d in (app, app / "bin", work, work / "pgdata", work / "pgdata" / "base",
              work / "sock"):
        sh("ownership and mode", ["ls", "-lnd", str(d)])
    sh("free space on the work filesystem", ["df", "-h", str(work)])
    sh("free space on /app", ["df", "-h", str(app)])
    sh("what the data directory looks like", ["ls", "-ln", str(work / "pgdata")])
    (out / "env-diag.txt").write_text("\n".join(lines))

def main():
    # Every file this verifier writes for the server to read -- pg_hba.conf,
    # postgresql.conf -- gets its mode from the umask, and the umask is
    # inherited from whatever started the container.  A pod and a `docker run`
    # do not necessarily agree on it, so a package that works at 0022 can score
    # 0.0 at 0077 with nothing else different.  Set it here rather than inherit
    # it, so the modes are a property of the package.  (The ownership is handled
    # separately: serverctl hands the parent to the server user before initdb,
    # and chowns the data directory after writing into it.)
    os.umask(0o022)
    import argparse
    ap = argparse.ArgumentParser()
    ap.add_argument("--corpus", required=True)
    ap.add_argument("--app", default="/app")
    ap.add_argument("--out", default="/logs/verifier")
    ap.add_argument("--work", default="/tmp/pgvc")
    ap.add_argument("--workers", type=int, default=6)
    a = ap.parse_args()

    corpus = pathlib.Path(a.corpus)
    # argparse hands back strings.  Everything downstream does path arithmetic on
    # this, and the one place that did it on the raw string -- bringup_diagnostics,
    # which only ever runs when a build or a start FAILS -- crashed the verifier
    # the first time a candidate failed to build.  Normalise once, here.
    app = pathlib.Path(a.app)
    out = pathlib.Path(a.out); out.mkdir(parents=True, exist_ok=True)
    work = pathlib.Path(a.work); work.mkdir(parents=True, exist_ok=True)
    # The components' results are written and read back here as root.  `work` is
    # handed to the server user before initdb (it is the data directory's
    # parent), so the results go to a directory of root's own instead, made
    # before any candidate code runs, where the server user cannot add, swap or
    # link a name.
    results = pathlib.Path(tempfile.mkdtemp(prefix="pgvc-results-"))
    weights = json.loads((corpus / "weights.json").read_text())
    report = {"components": {}, "gate": {}}
    # Which recorded-answer directory this run actually read.  A report showing a
    # neighbour's rung is a wrong-corpus delivery made visible instead of silent.
    report["gate"]["expected_dir"] = expected_root(corpus).name
    # Taken before anything the candidate controls can run, so that every exit
    # path -- including the early ones -- is compared against the same baseline.
    # Whatever directories this verifier is actually made of, found from where
    # this file lives and from the corpus it was pointed at -- not a hardcoded
    # /tests, which would make the check a no-op anywhere else and a guard that
    # cannot fire is not a guard.
    fp_roots = [HERE, pathlib.Path(a.corpus)]
    if (HERE.parent / "test.sh").exists():        # the packaged layout
        fp_roots = [HERE.parent]
    fp_before, fp_n = tests_fingerprint(*fp_roots)
    report["gate"]["verifier_files"] = fp_n
    state = {"total": 0.0}
    roles_path = corpus / "roles.json"
    roles = json.loads(roles_path.read_text()) if roles_path.exists() else {}
    # units[component][role] = [passed, total]
    units = {}

    def note_units(comp, milestone, regression):
        units[comp] = {"milestone": list(milestone), "regression": list(regression)}
        e = report["components"].setdefault(comp, {})
        for role, (pp, tt) in (("milestone", milestone), ("regression", regression)):
            if tt:
                e[role] = {"passed": pp, "total": tt, "score": round(pp / tt, 4)}

    def finish(reward):
        # Anything the candidate ran -- the build, the server, a child of
        # either -- had the whole run to change what it was scored against.
        fp_after, _ = tests_fingerprint(*fp_roots)
        if fp_before is not None and fp_after is not None and fp_after != fp_before:
            report["verifier_tampered"] = (
                "the verifier's own files changed during the run (%s -> %s); the "
                "candidate was scored against something it could edit, so this "
                "run scores zero" % (fp_before[:16], fp_after[:16]))
            reward = 0.0
        jdump(out / "report.json", report)
        (out / "reward.json").write_text(
            json.dumps({"reward": round(max(0.0, min(1.0, reward)), 6)}))
        print(json.dumps({"reward": reward,
                          "components": {k: v.get("score") for k, v in report["components"].items()}},
                         indent=1))
        # The authoritative number, on stdout, where the candidate cannot reach
        # it.  test.sh reads this line and writes reward.json from it; a reward
        # file with no line behind it is distrusted and replaced with zero.
        print("HARNESS_REWARD %.6f" % max(0.0, min(1.0, reward)))
        return 0

    state["deadline"] = time.time() + COMPONENT_BUDGET_S   # reset once the gates are through

    def time_left():
        return max(0.0, state["deadline"] - time.time())

    def guard(name, fn):
        if time_left() < 30:
            report["components"].setdefault(name, {})["error"] = \
                "skipped: the component budget of %ds was already spent" % COMPONENT_BUDGET_S
            return
        # Components are scored independently, which means a server that dies
        # during one of them must not take the rest down with it: bring it back
        # before each component and let this one score what it can.
        srv = state.get("srv")
        if srv is not None and srv.proc is not None and srv.proc.poll() is not None:
            report.setdefault("restarts", []).append(name)
            srv.stop()
            if not srv.start(timeout=90):
                report["components"].setdefault(name, {})["error"] = \
                    "the server was not running and did not restart"
                return
        try:
            fn()
        except Exception as exc:
            report["components"].setdefault(name, {})["error"] = \
                "%s: %s" % (type(exc).__name__, str(exc)[:300])

    # ------------------------------------------------------------------ gates
    serverctl.clean_products(app)
    problems = serverctl.scan_tree(app)
    if problems:
        report["gate"]["tree_scan"] = problems
        return finish(0.0)
    report["gate"]["tree_scan"] = "clean"

    t0 = time.time()
    ok, buildlog = serverctl.build(app)
    report["gate"]["build"] = "ok" if ok else "failed"
    report["gate"]["build_seconds"] = round(time.time() - t0, 1)
    # Names only.  A build is the candidate's code running; what it is handed
    # should be a decision, and the decision should be visible in the report.
    report["gate"]["build_env_dropped"] = serverctl.dropped_build_vars()
    (out / "build.log").write_text(buildlog)
    if not ok:
        bringup_diagnostics(app, work, out)
        return finish(0.0)

    # scan_binaries also holds both programs to C against the C library alone;
    # tree_runtime_problems sweeps every other ELF the build left for another
    # language's runtime.
    post = (serverctl.scan_tree(app) + serverctl.scan_binaries(app)
            + serverctl.tree_runtime_problems(app))
    # Informational only, and taken before the branch so the report says the
    # same thing whether or not the scan found a real problem.
    binnotes = serverctl.note_binaries(app)
    if binnotes:
        report["gate"]["post_build_scan_notes"] = binnotes
    if post:
        report["gate"]["post_build_scan"] = post
        return finish(0.0)
    report["gate"]["post_build_scan"] = "clean"
    # The build is over, so the image's own interpreters and runtimes -- the
    # scorer's Python among them -- go out of the server user's reach before
    # either program starts: a name built at run time cannot load them either.
    report["gate"]["runtimes_locked"] = len(serverctl.lock_foreign_runtimes())
    # Bind the scan to the BYTES, not to the path: /app is handed to the server
    # user before initdb, so a delayed writer could replace the scanned file and
    # leave a path comparison satisfied.
    bin_digests = serverctl.binary_digests(app)
    report["gate"]["scanned_binaries"] = {k: (v[:16] if v else None)
                                          for k, v in bin_digests.items()}

    hbaf = corpus / "pg_hba.conf"
    srv = serverctl.Server(app, work / "pgdata", sockdir=work / "sock",
                           hba=hbaf.read_text() if hbaf.exists() else None)
    ok, initlog = srv.initdb()
    report["gate"]["initdb"] = "ok" if ok else "failed"
    (out / "initdb.log").write_text(initlog)
    if not ok:
        bringup_diagnostics(app, work, out)
        return finish(0.0)
    if not srv.start(timeout=90):
        report["gate"]["start"] = "failed"
        (out / "server.log").write_text(srv.tail_log(20000))
        bringup_diagnostics(app, work, out)
        srv.stop()
        return finish(0.0)
    # Self-asserting: ask, as the user the server actually runs as, whether the
    # expectations are reachable.  A defence that fails silently is worse than
    # none, so the answer goes into the report either way.
    report["gate"]["expectations_readable_by_server"] = probe_expectation_access(corpus)
    # The same probe in the other direction.  A permission model that hides the
    # answers can just as easily hide the inputs, and that failure looks like a
    # broken candidate rather than a broken verifier.
    report["gate"]["workspace_usable_by_server"] = probe_workspace_access(app, work)
    # The same boundary, asked of the reward rather than of the expectations.
    report["gate"]["reward_writable_by_server"] = probe_reward_writable(out)
    report["gate"]["server_uid"] = serverctl.server_uid(srv.proc)
    if str(report["gate"]["server_uid"]).startswith("NOT DROPPED"):
        # A root server can read the expectations it is being graded against, so
        # the run has no evidentiary value at all.  Do not score it.
        report["verifier_warning"] = (
            "the server did not drop privileges (%s); a root server can read the "
            "expectations, so this run is void"
            % report["gate"]["server_uid"])
        srv.stop()
        return finish(0.0)
    # The program now serving must be the blob that was scanned, not merely a
    # file at the same path.
    report["gate"]["running_binary"] = serverctl.running_binary_matches(
        srv.proc, app, bin_digests)
    if not str(report["gate"]["running_binary"]).startswith("ok"):
        srv.stop()
        return finish(0.0)
    running = serverctl.running_program(srv.proc, app)
    if running:
        report["gate"]["running_program"] = running
        srv.stop()
        return finish(0.0)
    report["gate"]["running_program"] = "ok"
    # ... and it is a C program: no other language's runtime is running as the
    # server's user or mapped into anything that is.  Asked again at the end,
    # because a runtime can be loaded after the first query as well as before.
    foreign = serverctl.foreign_runtime_running(srv.proc)
    if foreign:
        report["gate"]["foreign_runtime"] = foreign
        (out / "server.log").write_text(srv.tail_log(20000))
        srv.stop()
        return finish(0.0)
    report["gate"]["foreign_runtime"] = "none"
    report["gate"]["start"] = "ok"
    state["srv"] = srv
    state["deadline"] = time.time() + COMPONENT_BUDGET_S
    port = srv.port

    # ------------------------------------------------------------ components
    def sql_component():
        res = results / "sqlres"
        t = time.time()
        subprocess.run([sys.executable, str(HERE / "pgvc_sql.py"),
                        "--cases-dir", str(corpus / "cases"),
                        "--manifest", str(corpus / "manifest.json"),
                        "--out", str(res), "--host", "127.0.0.1", "--port", str(port),
                        "--workers", str(a.workers), "--case-timeout", "60"],
                       timeout=max(60.0, min(1800.0, time_left())))
        man = json.loads((corpus / "manifest.json").read_text())
        rung = int(man.get("rung", 0))
        passed, failed, details = 0, 0, []
        failed_names = []
        # Cases carry a role.  MILESTONE is what this release changes: the cases
        # this milestone adds, and every earlier case whose correct answer flips
        # here.  REGRESSION is earlier behaviour that does not change, which the
        # previous milestone's server already satisfies.  Scoring them as one
        # pool would pay most of the component out for work already done, so the
        # two are scored separately and multiplied at the reward level.
        role = {"milestone": [0, 0], "regression": [0, 0]}
        for c in man["cases"]:
            exp = expected_root(corpus) / "sql" / (c["name"] + ".out")
            got = res / (c["name"] + ".out")
            e = exp.read_bytes() if exp.exists() else b"<missing expectation>"
            g = got.read_bytes() if got.exists() else b"<no result>"
            ok_case = (e == g)
            r = "milestone" if (c.get("rung") == rung or c.get("flip")) else "regression"
            role[r][1] += 1
            if ok_case:
                role[r][0] += 1
                passed += 1
            else:
                failed += 1
                # The names of every failure, always -- a truncated list turns
                # "the seed already failed this" into "the candidate broke it",
                # which is the difference between an inherited miss and a
                # regression.  Only the expected/got TEXT is capped, because
                # that is what makes the file large.
                failed_names.append({"case": c["name"], "role": r,
                                     "stratum": c.get("stratum"),
                                     "flip": bool(c.get("flip", False))})
                if len(details) < 60:
                    details.append({"case": c["name"], "stratum": c.get("stratum"),
                                    "role": r, "flip": c.get("flip", False),
                                    "expected": e.decode("utf-8", "replace")[:600],
                                    "got": g.decode("utf-8", "replace")[:600]})
        jdump(out / "sql_failures.json", details)
        jdump(out / "sql_failed_cases.json", failed_names)
        mp, mt = role["milestone"]
        rp, rt = role["regression"]
        report["components"]["sql"] = {
            "passed": passed, "total": passed + failed,
            "score": round(passed / (passed + failed), 4) if (passed + failed) else 0.0,
            "seconds": round(time.time() - t, 1)}
        note_units("sql", (mp, mt), (rp, rt))

    def json_component(name, runner_args, expected_name, weight_key, drop=(), timeout=1200):
        def run():
            t = time.time()
            res = results / (name + ".json")
            subprocess.run([sys.executable] + runner_args + ["--out", str(res)],
                           timeout=max(30.0, min(float(timeout), time_left())))
            exp = json.loads((expected_root(corpus) / expected_name).read_text())
            got = json.loads(res.read_text()) if res.exists() else {}
            keys = [k for k in exp if not any(k.endswith(d) for d in drop)]
            p = sum(1 for k in keys if got.get(k) == exp[k])
            jdump(out / (name + "_failures.json"),
                  {k: {"expected": exp[k], "got": got.get(k)} for k in keys if got.get(k) != exp[k]})
            r = roles.get(name, {})
            mp = mt = rp = rt = 0
            for k in keys:
                ok_k = (got.get(k) == exp[k])
                if r.get(k, "milestone") == "milestone":
                    mt += 1; mp += ok_k
                else:
                    rt += 1; rp += ok_k
            report["components"][name] = {"passed": p, "total": len(keys),
                                          "score": round(p / len(keys), 4) if keys else 0.0,
                                          "seconds": round(time.time() - t, 1)}
            note_units(name, (mp, mt), (rp, rt))
        return run

    def datadir_component():
        import pgvc_datadir
        got = pgvc_datadir.collect(srv.datadir)
        exp = json.loads((expected_root(corpus) / "datadir.json").read_text())
        keys = ["PG_VERSION", "present"]
        p = sum(1 for k in keys if got.get(k) == exp.get(k))
        jdump(out / "datadir_result.json", {"expected": exp, "got": got})
        r = roles.get("datadir", {})
        mp = mt = rp = rt = 0
        for k in keys:
            ok_k = (got.get(k) == exp.get(k))
            if r.get(k, "milestone") == "milestone":
                mt += 1; mp += ok_k
            else:
                rt += 1; rp += ok_k
        report["components"]["datadir"] = {"passed": p, "total": len(keys),
                                           "score": round(p / len(keys), 4) if keys else 0.0}
        note_units("datadir", (mp, mt), (rp, rt))

    try:
        guard("sql", sql_component)
        names = (corpus / "protocol_names.txt").read_text().split()
        if names:
            guard("protocol", json_component(
                "protocol",
                [str(HERE / "pgvc_protocol.py"), "--port", str(port),
                 "--sockdir", str(work / "sock"), "--names", ",".join(names)],
                "protocol.json", "protocol", timeout=900))
        if (corpus / "specs.json").exists():
            guard("isolation", json_component(
                "isolation",
                [str(HERE / "pgvc_isolation.py"), "--specs", str(corpus / "specs.json"),
                 "--port", str(port)],
                "isolation.json", "isolation", timeout=1200))
        if (expected_root(corpus) / "datadir.json").exists():
            guard("datadir", datadir_component)
        if (corpus / "compat_queries.json").exists():
            guard("compat", json_component(
                "compat",
                [str(HERE / "pgvc_compat.py"), "--dumps", str(corpus / "dumps"),
                 "--queries", str(corpus / "compat_queries.json"), "--port", str(port)],
                "compat.json", "compat", drop=("restore_detail",), timeout=900))
    finally:
        try:
            state["foreign_end"] = serverctl.foreign_runtime_running(srv.proc)
        except Exception as exc:
            state["foreign_end"] = ["the end-of-run runtime check failed: %s: %s"
                                    % (type(exc).__name__, str(exc)[:300])]
        (out / "server.log").write_text(srv.tail_log(20000))
        srv.stop()
    if state.get("foreign_end"):
        report["gate"]["foreign_runtime_end"] = state["foreign_end"]
        return finish(0.0)
    report["gate"]["foreign_runtime_end"] = "none"

    # The reward combines the two roles rather than the components:
    #   T1 = the milestone role: what this release changed -- its own cases and
    #        every earlier case whose correct answer flips here;
    #   T2 = the regression role: earlier behaviour this release keeps.
    #   reward = T1 x T2.
    # The previous release's server, unchanged, keeps T2 = 1 but scores T1 near
    # zero, so it earns almost nothing for the earlier behaviour it already had;
    # a tree that breaks an earlier release loses that fraction of its own
    # milestone.  Within each role the component weights apply, renormalised
    # over the components that have any unit of that role.
    def role_score(role):
        num = den = 0.0
        for comp, u in units.items():
            pp, tt = u[role]
            if not tt:
                continue
            w = weights.get(comp, 0.0)
            num += w * (pp / tt)
            den += w
        return (num / den) if den else None

    m, r = role_score("milestone"), role_score("regression")
    # No measured milestone unit means this release's own work was not shown,
    # so T1 = 0.  No regression unit (milestone 1, where nothing came before)
    # means there is nothing earlier to keep, so T2 = 1 and the reward is T1.
    t1 = m if m is not None else 0.0
    t2 = r if r is not None else 1.0
    total = t1 * t2
    report["reward_formula"] = "T1 (milestone role) x T2 (regression role)"
    report["roles"] = {
        "milestone": {"score": round(m, 6) if m is not None else None,
                      "factor": "T1", "factor_value": round(t1, 6),
                      "units": {c: units[c]["milestone"] for c in units if units[c]["milestone"][1]}},
        "regression": {"score": round(r, 6) if r is not None else None,
                       "factor": "T2", "factor_value": round(t2, 6),
                       "units": {c: units[c]["regression"] for c in units if units[c]["regression"][1]}},
    }
    return finish(total)


def guarded_main():
    """Rule 8: the step always writes reward.json and exits 0.

    A verifier that dies before it can write one leaves the harness to invent a
    number, and the reason for the death goes with it.  Anything that escapes
    main() is caught here, recorded where a reader will find it, and reported as
    the 0.0 it is."""
    try:
        return main()
    except BaseException:
        import traceback
        tb = traceback.format_exc()
        # Honour --out: a crash that writes its evidence somewhere the caller is
        # not looking is a crash with no evidence, which is this handler's whole
        # purpose.  argparse is gone by now, so read it back off argv.
        outdir = "/logs/verifier"
        for _k, _tok in enumerate(sys.argv):
            if _tok == "--out" and _k + 1 < len(sys.argv):
                outdir = sys.argv[_k + 1]
            elif _tok.startswith("--out="):
                outdir = _tok.split("=", 1)[1]
        out = pathlib.Path(outdir)
        try:
            out.mkdir(parents=True, exist_ok=True)
            (out / "crash.txt").write_text(tb)
            rep = {}
            rj = out / "report.json"
            if rj.exists():
                try:
                    rep = json.loads(rj.read_text())
                except Exception:
                    rep = {}
            rep["verifier_crashed"] = tb.splitlines()[-1][:300]
            rep["verifier_traceback"] = tb[-4000:]
            jdump(rj, rep)
            (out / "reward.json").write_text(json.dumps({"reward": 0.0}))
        except Exception:
            pass
        sys.stderr.write(tb)
        print(json.dumps({"reward": 0.0, "error": "the verifier crashed; "
                          "see %s" % (out / "crash.txt")}))
        print("HARNESS_REWARD 0.000000")
        return 0


if __name__ == "__main__":
    sys.exit(guarded_main())
