#!/usr/bin/env python3
"""Build the fixed, stratified t/ selection for a step: the test points that
both the pinned reference git and the reference implementation pass, measured
the way they are scored -- the whole file runs and the recorded points are read
out of its TAP -- so scoring is a lookup rather than a judgement call."""
import argparse, concurrent.futures, json, os, re, subprocess, sys


AS_USER = None


def _wrap(cmd_list, cwd):
    """Selection has to measure what scoring measures, and scoring runs the
    candidate unprivileged: a test whose prerequisite depends on being root
    (test-lib's SANITY) behaves differently under the two identities."""
    if AS_USER is None:
        return cmd_list, cwd
    inner = "cd %s && %s" % (cwd, " ".join(cmd_list))
    return (["setpriv", "--reuid=%d" % AS_USER, "--regid=%d" % AS_USER,
             "--clear-groups", "--inh-caps=-all", "/bin/bash", "-c", inner], None)


def run_file(tfile, gitbin, suite, outdir, timeout=900, runlist=None):
    env = dict(os.environ)
    env.update({
        "GIT_TEST_INSTALLED": gitbin,
        "TEST_NO_MALLOC_CHECK": "1",
        "HOME": os.path.join(outdir, "home"),
        "TEST_OUTPUT_DIRECTORY": os.path.join(outdir, os.path.basename(tfile) + ".out"),
        "LC_ALL": "C", "LANG": "C", "TZ": "UTC",
        "GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME": "master",
    })
    os.makedirs(env["HOME"], exist_ok=True)
    os.makedirs(env["TEST_OUTPUT_DIRECTORY"], exist_ok=True)
    cmd = ["./" + tfile, "--no-chain-lint"]
    if runlist:
        cmd.append("--run=" + ",".join(str(n) for n in runlist))
    try:
        cmd, cwd = _wrap(cmd, os.path.join(suite, "t"))
        p = subprocess.run(cmd, cwd=cwd, env=env,
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=timeout)
    except subprocess.TimeoutExpired:
        return set(), set()
    except OSError:
        return set(), set()
    ok, notok = set(), set()
    for line in p.stdout.decode("utf-8", "replace").splitlines():
        m = re.match(r"^ok (\d+)(.*)$", line)
        if m:
            # `ok N # skip ...` is an unmet prerequisite, not a pass: a point
            # that only ever skips would be free credit for a stub
            if re.match(r"\s*#\s*skip", m.group(2), re.I):
                continue
            ok.add(int(m.group(1)))
            continue
        m = re.match(r"^not ok (\d+)", line)
        if m:
            notok.add(int(m.group(1)))
    return ok, notok


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--files", required=True)
    ap.add_argument("--reference", required=True)
    ap.add_argument("--candidate", required=True)
    ap.add_argument("--suite", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--rounds", type=int, default=2)
    ap.add_argument("--timeout", type=int, default=600)
    ap.add_argument("--as-user", type=int, default=None,
                    help="run every t/ file as this uid, as the scorer does")
    args = ap.parse_args()
    global AS_USER
    AS_USER = args.as_user
    files = [l.strip() for l in open(args.files) if l.strip() and not l.startswith("#")]
    # t0008-ignores.sh needs submodule/gitlink `git add` (mode 160000), which is
    # orthogonal to this task's format/protocol theme and which the reference
    # impl does not provide; its setup therefore fails and no point in it can
    # satisfy the "reference impl passes it" selection invariant.  Exclude it so
    # the component-A denominator only ever holds points the oracle really passes.
    SELECT_EXCLUDE = {"t0008-ignores.sh"}
    files = [f for f in files if os.path.basename(f) not in SELECT_EXCLUDE]

    def one(f):
        both = None
        for r in range(args.rounds):
            rok, _ = run_file(f, args.reference, args.suite, "/tmp/selout-ref%d" % r, args.timeout)
            cok, _ = run_file(f, args.candidate, args.suite, "/tmp/selout-cand%d" % r, args.timeout)
            inter = rok & cok
            both = inter if both is None else (both & inter)
        return f, sorted(both)

    sel = {}
    with concurrent.futures.ThreadPoolExecutor(max_workers=7) as pool:
        for f, pts in pool.map(one, files):
            sel[f] = pts
            print("%-42s %d points" % (f, len(pts)), flush=True)
    sel = {k: v for k, v in sel.items() if v}
    with open(args.out, "w") as fh:
        json.dump(sel, fh, indent=1)
    print("total selected: %d across %d files" % (sum(len(v) for v in sel.values()), len(sel)))


if __name__ == "__main__":
    main()
