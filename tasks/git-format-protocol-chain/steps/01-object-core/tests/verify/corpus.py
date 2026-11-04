#!/usr/bin/env python3
"""Build a corpus of repositories with pinned real gits (record mode) and read
them back with a candidate git (check mode).

record: writes <out>/<name>.tar plus expect.json, the output of every read
        probe as produced by the pinned reference git.
check:  unpacks each repository into a scratch directory, runs the same probes
        with the candidate git and reports which matched.
"""
# Every command a grader starts runs under an address-space ceiling and a
# wall clock, so one runaway process cannot take the node down.
LIMITS = "ulimit -v 4194304 2>/dev/null; ulimit -t 600 2>/dev/null; "

import argparse
import hashlib, json, os, shutil, subprocess, tarfile, tempfile

ENV = {
    "GIT_AUTHOR_NAME": "A U Thor", "GIT_AUTHOR_EMAIL": "author@example.com",
    "GIT_COMMITTER_NAME": "C O Mitter", "GIT_COMMITTER_EMAIL": "committer@example.com",
    "GIT_AUTHOR_DATE": "@1112911993 +0000", "GIT_COMMITTER_DATE": "@1112911993 +0000",
    "GIT_CONFIG_NOSYSTEM": "1", "GIT_ATTR_NOSYSTEM": "1",
    "TZ": "UTC", "LC_ALL": "C", "LANG": "C", "GIT_PAGER": "cat", "PAGER": "cat",
}

# Every repository is built by a real git; the candidate only ever reads them.
BUILDS = {
    "basic": """
git init -q -b main . &&
printf 'alpha\\n' > a.txt && mkdir -p dir/sub && printf 'beta\\n' > dir/sub/b.txt &&
printf 'run\\n' > run.sh && chmod +x run.sh && ln -s a.txt link.txt &&
printf '*.log\\nbuild/\\n' > .gitignore &&
git add -A && git commit -q -m 'initial import' &&
printf 'alpha2\\n' >> a.txt && git add a.txt &&
GIT_AUTHOR_DATE='@1112912000 +0000' GIT_COMMITTER_DATE='@1112912000 +0000' git commit -q -m 'second commit' &&
git tag -a v1.0 -m 'release 1.0' && git tag light-tag HEAD~1 &&
git branch topic HEAD~1 &&
git checkout -q topic &&
printf 'topic\\n' > t.txt && git add t.txt &&
GIT_AUTHOR_DATE='@1112912100 +0000' GIT_COMMITTER_DATE='@1112912100 +0000' git commit -q -m 'topic work' &&
git checkout -q main &&
GIT_AUTHOR_DATE='@1112912200 +0000' GIT_COMMITTER_DATE='@1112912200 +0000' git merge -q --no-ff -m 'merge topic' topic
""",
    "packed": """
git init -q -b main . &&
printf 'x\\n' > f && git add f && git commit -q -m one &&
printf 'y\\n' >> f && git add f &&
GIT_AUTHOR_DATE='@1112912000 +0000' GIT_COMMITTER_DATE='@1112912000 +0000' git commit -q -m two &&
git branch b1 && git branch b2 && git tag -a t1 -m tag && git tag t2 &&
git pack-refs --all
""",
    "deep": """
git init -q -b main . &&
mkdir -p a/b/c/d/e &&
printf '1\\n' > a/b/c/d/e/deep.txt &&
printf '2\\n' > 'a/b/name with space.txt' &&
printf '3\\n' > 'a/quote"name' &&
printf '4\\n' > 'a/utf8-\\303\\244\\303\\266.txt' &&
git add -A && git commit -q -m 'deep tree'
""",
    "empty": """
git init -q -b main .
""",
    "detached": """
git init -q -b main . &&
printf '1\\n' > f && git add f && git commit -q -m one &&
printf '2\\n' > f && git add f &&
GIT_AUTHOR_DATE='@1112912000 +0000' GIT_COMMITTER_DATE='@1112912000 +0000' git commit -q -m two &&
git checkout -q --detach HEAD~1
""",
    "unborn-branch": """
git init -q -b main . &&
printf 'staged\\n' > s && git add s
""",
}

# Deliberate corruptions applied after the build, with the expected fsck verdict.
CORRUPTIONS = {
    "corrupt-hash": "obj_mutate",
    "corrupt-truncate": "obj_truncate",
    "missing-blob": "obj_delete",
    "dangling": "dangle",
}

PROBES = [
    "git rev-parse --show-object-format",
    "git rev-parse HEAD 2>&1; echo exit=$?",
    "git log --format='%H %T %P %an %ad %s' 2>&1; echo exit=$?",
    "git log --oneline 2>&1 | head -20; echo exit=$?",
    "git ls-tree -r HEAD 2>&1; echo exit=$?",
    "git ls-tree -r -t HEAD 2>&1; echo exit=$?",
    "git cat-file -p HEAD 2>&1; echo exit=$?",
    "git cat-file --batch-all-objects --batch-check 2>/dev/null | sort",
    "git ls-files -s",
    "git status --porcelain=v1 2>&1; echo exit=$?",
    "git show-ref 2>&1; echo exit=$?",
    "git for-each-ref --format='%(refname) %(objecttype) %(objectname)' 2>&1",
    "git rev-list --all --count 2>&1; echo exit=$?",
    {"cmd": "git rev-list --objects --all 2>/dev/null | sort", "clean_only": True},
    "git fsck 2>&1 | sed 's/[0-9a-f]\\{40\\}/OID/g' | sort; echo exit=${PIPESTATUS[0]}",
    "git count-objects",
    "git symbolic-ref -q HEAD; echo exit=$?",
    "git branch 2>&1",
    "git tag 2>&1",
    "git merge-base --all HEAD HEAD 2>&1; echo exit=$?",
    "git diff --stat HEAD 2>&1 | head -10; echo exit=$?",
]


PEERS_DIR = None



# Commands that run the candidate drop to an unprivileged user when the scorer
# asks for it: the grader's own files stay root-only, so nothing the candidate
# runs can read the expectations or rewrite the scorer.
AS_USER = None


def wrap(cmd):
    if AS_USER is None:
        return cmd
    return ("setpriv --reuid=%d --regid=%d --clear-groups --inh-caps=-all "
            "/bin/bash -c %s" % (AS_USER, AS_USER, "'" + cmd.replace("'", "'\\''") + "'"))


def own(path):
    if AS_USER is None:
        return
    # Root walks a tree the candidate uid writes between calls: never follow a
    # planted symlink out of it (chown of the link itself, never its target).
    # A top directory the candidate replaced with a symlink is not walked: the
    # walk would list, and chown, whatever the link points at.
    try:
        if os.path.islink(path):
            os.chown(path, AS_USER, AS_USER, follow_symlinks=False)
            return
        for root, dirs, files in os.walk(path):
            os.chown(root, AS_USER, AS_USER, follow_symlinks=False)
            for n in dirs + files:
                try:
                    os.chown(os.path.join(root, n), AS_USER, AS_USER,
                             follow_symlinks=False)
                except OSError:
                    pass
        os.chown(path, AS_USER, AS_USER, follow_symlinks=False)
    except OSError:
        pass


def write_wrapper(link, gitbin):
    """Root writes the `git` exec wrapper into a directory the candidate uid may
    own: remove whatever is there, then create the file exclusively without
    following a symlink, so a planted (or dangling) link can never redirect a
    root write elsewhere."""
    try:
        os.unlink(link)
    except FileNotFoundError:
        pass
    fd = os.open(link, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW, 0o755)
    try:
        os.write(fd, ("#!/bin/sh\nexec %s \"$@\"\n" % gitbin).encode())
        os.fchmod(fd, 0o755)
    finally:
        os.close(fd)

def sh(cmd, cwd, gitbin, extra_env=None, check=False):
    env = dict(os.environ)
    env.update(ENV)
    env["HOME"] = os.path.join(cwd, "..", "home")
    os.makedirs(env["HOME"], exist_ok=True)
    bindir = os.path.join(os.path.dirname(os.path.abspath(cwd)), "bin")
    os.makedirs(bindir, exist_ok=True)
    link = os.path.join(bindir, "git")
    # exec wrapper, not a symlink (see workload.py)
    write_wrapper(link, gitbin)
    env["PATH"] = bindir + ":/usr/bin:/bin:/usr/sbin:/sbin"
    # the scratch root, the HOME and the bin directory are created by this
    # process (root) and used by the candidate (unprivileged)
    own(os.path.dirname(os.path.abspath(cwd)))
    if PEERS_DIR:
        env["PEER"] = os.path.join(PEERS_DIR, "v2.51.0", "bin", "git")
    if extra_env:
        env.update(extra_env)
    p = subprocess.run(["/bin/bash", "-c", wrap(LIMITS + cmd)], cwd=cwd, env=env,
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=300)
    if check and p.returncode != 0:
        raise SystemExit("build failed: %s\n%s" % (cmd, p.stdout.decode()))
    return p.returncode, p.stdout.decode("utf-8", "replace")


def corrupt(kind, repo, gitbin):
    """Apply one deliberate corruption to a freshly built repository."""
    objdir = os.path.join(repo, ".git", "objects")
    loose = []
    for d in sorted(os.listdir(objdir)):
        if len(d) != 2:
            continue
        for f in sorted(os.listdir(os.path.join(objdir, d))):
            loose.append((d + f, os.path.join(objdir, d, f)))
    packs = sorted(__import__("glob").glob(os.path.join(objdir, "pack", "*.pack")))
    if kind == "pack_truncate" and packs:
        data = open(packs[0], "rb").read()
        os.chmod(packs[0], 0o644)
        open(packs[0], "wb").write(data[: max(32, len(data) // 2)])
        return
    if kind == "pack_mutate" and packs:
        data = bytearray(open(packs[0], "rb").read())
        os.chmod(packs[0], 0o644)
        if len(data) > 80:
            data[len(data) // 2] ^= 0xFF
        open(packs[0], "wb").write(bytes(data))
        return
    if kind == "obj_mutate":
        # flip bytes in the zlib stream of the last object: hash cannot match
        oid, path = loose[-1]
        os.chmod(path, 0o644)
        data = bytearray(open(path, "rb").read())
        data[-1] ^= 0xFF
        open(path, "wb").write(bytes(data))
    elif kind == "obj_truncate":
        oid, path = loose[-1]
        os.chmod(path, 0o644)
        data = open(path, "rb").read()
        open(path, "wb").write(data[: max(1, len(data) // 2)])
    elif kind == "obj_delete":
        rc, out = sh("git rev-parse HEAD:f 2>/dev/null || git rev-parse HEAD:a.txt", repo, gitbin)
        oid = out.strip().splitlines()[-1]
        p = os.path.join(objdir, oid[:2], oid[2:])
        if os.path.exists(p):
            os.unlink(p)
    elif kind == "dangle":
        sh("printf 'orphan object\\n' | git hash-object -w --stdin", repo, gitbin)
        sh("git branch tmpb && git commit -q --allow-empty -m dangling && git checkout -q main 2>/dev/null; "
           "git branch -D tmpb >/dev/null 2>&1 || true", repo, gitbin)


def record(gitbin, out):
    os.makedirs(out, exist_ok=True)
    expect = {}
    for name, script in BUILDS.items():
        root = tempfile.mkdtemp(prefix="cor-")
        repo = os.path.join(root, "r")
        os.makedirs(repo)
        sh(script, repo, gitbin, check=True)
        with tarfile.open(os.path.join(out, name + ".tar"), "w") as tf:
            tf.add(repo, arcname="r")
        expect[name] = probe_all(repo, gitbin, name in DAMAGED)
        shutil.rmtree(root, ignore_errors=True)
    base_build = BUILDS.get("basic") or next(iter(BUILDS.values()))
    for name, kind in CORRUPTIONS.items():
        root = tempfile.mkdtemp(prefix="cor-")
        repo = os.path.join(root, "r")
        os.makedirs(repo)
        sh(base_build, repo, gitbin, check=True)
        corrupt(kind, repo, gitbin)
        with tarfile.open(os.path.join(out, name + ".tar"), "w") as tf:
            tf.add(repo, arcname="r")
        expect[name] = probe_all(repo, gitbin, True)
        shutil.rmtree(root, ignore_errors=True)
    with open(os.path.join(out, "expect.json"), "w") as fh:
        json.dump(expect, fh, indent=1, sort_keys=True)
    print("recorded %d repositories, %d probes each" % (len(expect), len(PROBES)))


DAMAGED = set(CORRUPTIONS) | {"missing-blob"}


def probe_all(repo, gitbin, damaged=False):
    res = []
    for probe in PROBES:
        cmd = probe["cmd"] if isinstance(probe, dict) else probe
        if damaged and isinstance(probe, dict) and probe.get("clean_only"):
            continue
        rc, outp = sh(cmd, repo, gitbin)
        res.append({"cmd": cmd, "status": rc, "output": outp})
    return res


def digest(status, output):
    h = hashlib.sha256()
    h.update(b"%d\0" % int(status))
    h.update(output.encode("utf-8", "surrogateescape") + b"\0")
    return h.hexdigest()


def check(gitbin, corpusdir, outfile):
    with open(os.path.join(corpusdir, "expect.json")) as fh:
        expect = json.load(fh)
    results = []
    for name in sorted(expect):
        tar = os.path.join(corpusdir, name + ".tar")
        root = tempfile.mkdtemp(prefix="chk-")
        with tarfile.open(tar) as tf:
            tf.extractall(root)
        repo = os.path.join(root, "r")
        for want in expect[name]:
            rc, outp = sh(want["cmd"], repo, gitbin)
            # expectations ship as digests; see verify/digestify.py
            try:
                ok = (digest(rc, outp) == want["digest"]) if "digest" in want else (
                    rc == want["status"] and outp == want["output"])
            except Exception:  # noqa: BLE001
                ok = False      # one bad reply costs one probe, not the run
            results.append({"repo": name, "cmd": want["cmd"], "ok": ok,
                            "got": outp if not ok else "", "got_status": rc})
        shutil.rmtree(root, ignore_errors=True)
    with open(outfile, "w") as fh:
        json.dump(results, fh, indent=1)
    ok = sum(1 for r in results if r["ok"])
    print("corpus probes: %d/%d" % (ok, len(results)))


def main():
    global BUILDS, CORRUPTIONS, PROBES, DAMAGED
    ap = argparse.ArgumentParser()
    ap.add_argument("--mode", choices=["record", "check"], required=True)
    ap.add_argument("--git", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--corpus", default=None)
    ap.add_argument("--defs", default=None,
                    help="JSON with builds/corruptions/probes for this step")
    ap.add_argument("--peers", default=None)
    ap.add_argument("--as-user", type=int, default=None,
                    help="run every candidate command as this uid")
    args = ap.parse_args()
    global AS_USER
    AS_USER = args.as_user
    if args.defs:
        with open(args.defs) as fh:
            d = json.load(fh)
        BUILDS = d.get("builds", BUILDS)
        CORRUPTIONS = d.get("corruptions", CORRUPTIONS)
        PROBES = d.get("probes", PROBES)
        DAMAGED = set(CORRUPTIONS) | set(d.get("damaged", []))
    global PEERS_DIR
    PEERS_DIR = args.peers
    if args.mode == "record":
        record(os.path.abspath(args.git), args.out)
    else:
        check(os.path.abspath(args.git), args.corpus, args.out)


if __name__ == "__main__":
    main()
