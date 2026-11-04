#!/usr/bin/env python3
"""Interop: a repository is built by one git (the "builder"), then read by the
pinned real git peers.  Recording runs the builder as the reference git; scoring
runs it as the candidate, so every check asks "can real git make sense of what
this implementation wrote?".
"""
# Every command a grader starts runs under an address-space ceiling and a
# wall clock, so one runaway process cannot take the node down.
LIMITS = "ulimit -v 4194304 2>/dev/null; ulimit -t 600 2>/dev/null; "

import argparse, hashlib, json, os, re, shutil, stat, subprocess, tempfile

ENV = {
    "GIT_AUTHOR_NAME": "A U Thor", "GIT_AUTHOR_EMAIL": "author@example.com",
    "GIT_COMMITTER_NAME": "C O Mitter", "GIT_COMMITTER_EMAIL": "committer@example.com",
    "GIT_AUTHOR_DATE": "@1112911993 +0000", "GIT_COMMITTER_DATE": "@1112911993 +0000",
    "GIT_CONFIG_NOSYSTEM": "1", "GIT_ATTR_NOSYSTEM": "1",
    "TZ": "UTC", "LC_ALL": "C", "LANG": "C", "GIT_PAGER": "cat", "PAGER": "cat",
}



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

# A pinned real git that reads a candidate-built repository runs on that
# repository's own .git/config, which the candidate wrote.  It must not pick up
# anything that runs a program or changes what it prints: hooks, an fsmonitor
# command, an external diff or textconv driver, a pager, or a global or XDG
# config planted in a HOME the candidate could write.  So the peer is reached
# only through this root-written wrapper, in a root-owned directory that is the
# first entry of PATH (a candidate-writable directory there could shadow `sort`,
# `grep` or `md5sum` in a probe), with a root-owned empty HOME and the
# command-line overrides below, which outrank every config file.
PEER_WRAPPER = """#!/bin/sh
HOME=@HOME@; XDG_CONFIG_HOME=@HOME@/.config
GIT_CONFIG_NOSYSTEM=1; GIT_CONFIG_GLOBAL=/dev/null; GIT_ATTR_NOSYSTEM=1
export HOME XDG_CONFIG_HOME GIT_CONFIG_NOSYSTEM GIT_CONFIG_GLOBAL GIT_ATTR_NOSYSTEM
n=$#; seen=0; want=0
for a in "$@"; do
  if [ $want = 1 ]; then set -- "$@" "$a"; want=0; continue; fi
  if [ $seen = 0 ]; then
    case "$a" in
      -C|-c) set -- "$@" "$a"; want=1; continue;;
      -*) set -- "$@" "$a"; continue;;
      diff|log|show) set -- "$@" "$a" --no-ext-diff --no-textconv; seen=1; continue;;
      *) set -- "$@" "$a"; seen=1; continue;;
    esac
  fi
  set -- "$@" "$a"
done
shift $n
exec @PEER@ -c core.hooksPath=/dev/null -c core.fsmonitor= -c core.pager=cat -c diff.external= "$@"
"""
# Scenarios hand a client `--upload-pack="$(dirname $PEER)/git-upload-pack"`, so
# the peer's server side sits beside the wrapper, under the same overrides.
PEER_UPLOAD_PACK = """#!/bin/sh
HOME=@HOME@; XDG_CONFIG_HOME=@HOME@/.config
GIT_CONFIG_NOSYSTEM=1; GIT_CONFIG_GLOBAL=/dev/null; GIT_ATTR_NOSYSTEM=1
export HOME XDG_CONFIG_HOME GIT_CONFIG_NOSYSTEM GIT_CONFIG_GLOBAL GIT_ATTR_NOSYSTEM
exec @PEER@ -c core.hooksPath=/dev/null -c core.fsmonitor= -c core.pager=cat -c diff.external= upload-pack "$@"
"""


def make_peer_env(base, peerbin):
    """Root-owned bin/ (holding only the peer wrapper) and empty home/ under
    `base`, which the caller places where the candidate cannot write."""
    if os.path.lexists(base):
        if os.path.islink(base) or not os.path.isdir(base):
            os.unlink(base)
        else:
            shutil.rmtree(base)
    bindir = os.path.join(base, "bin")
    home = os.path.join(base, "home")
    os.makedirs(bindir)
    os.makedirs(home)
    for d in (base, bindir, home):
        os.chown(d, 0, 0, follow_symlinks=False)
        os.chmod(d, 0o755)
    for name, text in (("git", PEER_WRAPPER), ("git-upload-pack", PEER_UPLOAD_PACK)):
        fd = os.open(os.path.join(bindir, name),
                     os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW, 0o755)
        try:
            os.write(fd, text.replace("@HOME@", home).replace("@PEER@", peerbin).encode())
            os.fchmod(fd, 0o755)
        finally:
            os.close(fd)
    return {"bin": bindir, "home": home, "git": os.path.join(bindir, "git")}


# The command-line overrides in PEER_WRAPPER do not reach every real-git process
# that opens a candidate-built repository: a local push or fetch starts
# receive-pack/upload-pack inside it with GIT_CONFIG_PARAMETERS cleared, and a
# `filter.<driver>` defined there cannot be overridden by name at all.  So the
# peers probe a COPY that root makes just before the scenario's probes, whose
# config keeps only the keys below -- the ones a scenario's probes need -- and
# which has no hooks/ or commondir (config.worktree is reduced the same way).
# Everything else the candidate's config held (filter.*, diff.*, merge drivers,
# core command keys, include/includeIf, remote/url/protocol settings, aliases
# ...) is dropped.
CONFIG_KEEP = re.compile(
    r"^(core\.(repositoryformatversion|filemode|bare|logallrefupdates|symlinks|"
    r"ignorecase|precomposeunicode|sparsecheckout|sparsecheckoutcone|commitgraph|"
    r"multipackindex)"
    r"|extensions\.[a-z0-9]+"
    r"|index\.(version|sparse|skiphash)"
    r"|uploadpack\.(allowfilter|allowanysha1inwant|allowtipsha1inwant|"
    r"allowreachablesha1inwant|allowrefinwant))$")


def _plain_only(directory, names):
    """copytree ignore hook: never copy a FIFO, socket or device node."""
    skip = []
    for n in names:
        try:
            m = os.lstat(os.path.join(directory, n)).st_mode
        except OSError:
            skip.append(n)
            continue
        if not (stat.S_ISREG(m) or stat.S_ISDIR(m) or stat.S_ISLNK(m)):
            skip.append(n)
    return skip


def _reduce_gitdir(gitdir, cfg_git, cwd):
    """Rewrite gitdir/config (and config.worktree, where `sparse-checkout`
    keeps its settings) with only CONFIG_KEEP keys; drop hooks and the file
    that sends git to another repository."""
    for name in ("config", "config.worktree"):
        cfg = os.path.join(gitdir, name)
        if name == "config" or os.path.lexists(cfg):
            _reduce_config_file(cfg, cfg_git, cwd)
    for name in ("hooks", "commondir"):
        p = os.path.join(gitdir, name)
        if os.path.islink(p) or (os.path.lexists(p) and not os.path.isdir(p)):
            os.unlink(p)
        elif os.path.isdir(p):
            shutil.rmtree(p)


def _reduce_config_file(cfg, cfg_git, cwd):
    keep = []
    try:
        regular = stat.S_ISREG(os.lstat(cfg).st_mode)
    except OSError:
        regular = False
    if regular:
        env = {"PATH": "/usr/bin:/bin", "HOME": cwd, "GIT_CONFIG_NOSYSTEM": "1",
               "GIT_CONFIG_GLOBAL": "/dev/null", "LC_ALL": "C"}
        # --file reads that one file and, unlike a repository lookup, does not
        # follow include.path
        p = subprocess.run([cfg_git, "config", "--file", cfg, "--null", "--list"],
                           cwd=cwd, env=env, stdout=subprocess.PIPE,
                           stderr=subprocess.DEVNULL, timeout=60)
        if p.returncode == 0:
            for item in p.stdout.split(b"\0"):
                key, _, val = item.partition(b"\n")
                k = key.decode("utf-8", "replace")
                if k and CONFIG_KEEP.match(k):
                    keep.append((key, val))
    if os.path.lexists(cfg):
        os.unlink(cfg)
    open(cfg, "w").close()
    for key, val in keep:
        subprocess.run([cfg_git, "config", "--file", cfg, "--add", key, val],
                       cwd=cwd, env={"PATH": "/usr/bin:/bin", "HOME": cwd,
                                     "GIT_CONFIG_NOSYSTEM": "1"},
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=60)


def make_probe_copy(src_root, dst_root, cfg_git):
    """Root-made copy of a built scenario for the peers to probe (see above).
    Symlinks are copied as links, never followed; special files are skipped;
    a source that is itself a symlink is not read."""
    os.makedirs(dst_root)
    dst_repo = os.path.join(dst_root, "repo")
    src_repo = os.path.join(src_root, "repo")
    if (not os.path.islink(src_root) and os.path.isdir(src_repo)
            and not os.path.islink(src_repo)):
        shutil.copytree(src_repo, dst_repo, symlinks=True, ignore=_plain_only)
        # files the build left beside the repository (a bundle written to ../)
        for n in sorted(os.listdir(src_root)):
            if n in ("repo", "aux") or n.startswith(("peerwork-", "peerenv-")):
                continue
            p = os.path.join(src_root, n)
            if stat.S_ISREG(os.lstat(p).st_mode):
                shutil.copy2(p, os.path.join(dst_root, n), follow_symlinks=False)
    else:
        os.makedirs(dst_repo)
    dotgit = os.path.join(dst_repo, ".git")
    if os.path.lexists(dotgit) and (os.path.islink(dotgit) or not os.path.isdir(dotgit)):
        os.unlink(dotgit)          # a gitfile or link would lead outside the copy
    if os.path.isdir(dotgit):
        _reduce_gitdir(dotgit, cfg_git, dst_root)
    if os.path.lexists(os.path.join(dst_repo, "config")):
        _reduce_gitdir(dst_repo, cfg_git, dst_root)      # a bare repository
    own(dst_root)
    return dst_repo


def reap_as_user():
    """Before root makes a scenario's copy, nothing may still run as the
    unprivileged uid (a build leftover, or a candidate server a previous
    probe started).  This runs inside the verifier container only."""
    if AS_USER is None:
        return
    for _ in range(20):
        if subprocess.run(["pgrep", "-u", str(AS_USER)], stdout=subprocess.DEVNULL,
                          stderr=subprocess.DEVNULL).returncode != 0:
            return
        subprocess.run(["pkill", "-KILL", "-u", str(AS_USER)],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        subprocess.run(["sleep", "0.2"])


def run(cmd, cwd, env_extra=None, gitbin=None, timeout=300, aux=None, peer_env=None):
    """aux is a scratch directory kept OUTSIDE any repository under test.
    peer_env (from make_peer_env) runs a pinned real git instead of the builder:
    root-owned PATH and HOME, never the candidate-writable aux."""
    env = dict(os.environ)
    env.update(ENV)
    if peer_env:
        env["HOME"] = peer_env["home"]
        env["XDG_CONFIG_HOME"] = os.path.join(peer_env["home"], ".config")
        env["GIT_CONFIG_GLOBAL"] = "/dev/null"
        env["PATH"] = peer_env["bin"] + ":/usr/bin:/bin"
        gitbin = None
    else:
        aux = aux or os.path.join(os.path.dirname(os.path.abspath(cwd)), ".aux")
        env["HOME"] = os.path.join(aux, "home")
        os.makedirs(env["HOME"], exist_ok=True)
    if gitbin:
        bindir = os.path.join(aux, "bin")
        os.makedirs(bindir, exist_ok=True)
        link = os.path.join(bindir, "git")
        # exec wrapper, not a symlink (see workload.py)
        write_wrapper(link, gitbin)
        env["PATH"] = bindir + ":/usr/bin:/bin"
        own(aux)
    if env_extra:
        env.update(env_extra)
    try:
        own(cwd)
        p = subprocess.run(["/bin/bash", "-c", wrap(LIMITS + cmd)], cwd=cwd, env=env,
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=timeout)
        return p.returncode, p.stdout.decode("utf-8", "replace")
    except subprocess.TimeoutExpired:
        return 124, "timeout\n"


def digest(status, output):
    h = hashlib.sha256()
    h.update(b"%d\0" % int(status))
    h.update(output.encode("utf-8", "surrogateescape") + b"\0")
    return h.hexdigest()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--git", required=True, help="builder git (reference when recording, candidate when scoring)")
    ap.add_argument("--peers", required=True, help="directory holding <version>/bin/git")
    ap.add_argument("--spec", required=True)
    ap.add_argument("--out", default=None)
    ap.add_argument("--expect", default=None)
    ap.add_argument("--phase", choices=["all", "build", "probe"], default="all")
    ap.add_argument("--repos", default=None,
                    help="where built repositories live, so building and probing "
                         "can run in separate phases (the real gits are hidden "
                         "from the candidate while it builds)")
    ap.add_argument("--as-user", type=int, default=None,
                    help="run every candidate command as this uid")
    args = ap.parse_args()
    global AS_USER
    AS_USER = args.as_user
    with open(args.spec) as fh:
        spec = json.load(fh)
    expect = None
    if args.expect:
        with open(args.expect) as fh:
            expect = json.load(fh)
    results = []
    gitbin = os.path.abspath(args.git)
    probe_base = None
    cfg_git = None
    if args.phase != "build":
        # root-owned and unique: only root can rename or replace anything in it
        probe_base = tempfile.mkdtemp(prefix="xvprobe-")
        os.chmod(probe_base, 0o755)
        vers = sorted((v for v in os.listdir(args.peers)
                       if os.path.isfile(os.path.join(args.peers, v, "bin", "git"))),
                      key=lambda v: [int(x) for x in re.findall(r"\d+", v)])
        cfg_git = os.path.join(args.peers, vers[-1], "bin", "git")
    for sc in spec:
        if args.repos:
            root = os.path.join(args.repos, sc["name"])
            if args.phase != "probe":
                shutil.rmtree(root, ignore_errors=True)
                os.makedirs(root)
        else:
            root = tempfile.mkdtemp(prefix="xv-")
        repo = os.path.join(root, "repo")
        aux = os.path.join(root, "aux")
        if args.phase != "probe":
            os.makedirs(repo, exist_ok=True)
            brc, bout = run(sc["build"], repo, gitbin=gitbin, aux=aux)
            results.append({"scenario": sc["name"], "peer": "builder", "cmd": "<build>",
                            "status": brc, "output": ""})
            if args.phase == "build":
                continue
        # The peers probe a root-made copy (make_probe_copy), under a directory
        # root created for this run; their PATH and HOME (make_peer_env) sit
        # beside it in that root-owned directory, out of the candidate's reach.
        reap_as_user()
        proot = os.path.join(probe_base, sc["name"])
        prepo = make_probe_copy(root, proot, cfg_git)
        for peer in sc.get("peers", ["v2.51.0"]):
            peerbin = os.path.join(args.peers, peer, "bin", "git")
            penv = make_peer_env(os.path.join(probe_base, "peerenv-%s-%s" % (sc["name"], peer)),
                                 peerbin)
            for probe in sc["probes"]:
                work = os.path.join(proot, "peerwork-" + peer)
                os.makedirs(work, exist_ok=True)
                # @EXEC@ is the BUILDER's libexec: a probe can hand the peer
                # the builder's own upload-pack, which is how a v2 exchange is
                # driven against the candidate's server
                execdir = os.path.join(os.path.dirname(os.path.dirname(gitbin)),
                                       "libexec", "git-core")
                cmd = (probe.replace("@REPO@", prepo).replace("@WORK@", work)
                            .replace("@EXEC@", execdir))
                rc, outp = run(cmd, work, peer_env=penv)
                outp = (outp.replace(prepo, "<REPO>").replace(work, "<WORK>")
                            .replace(proot, "<ROOT>").replace(execdir, "<EXEC>"))
                results.append({"scenario": sc["name"], "peer": peer, "cmd": probe,
                                "status": rc, "output": outp})
        if not args.repos:
            shutil.rmtree(root, ignore_errors=True)
    reap_as_user()
    if probe_base:
        shutil.rmtree(probe_base, ignore_errors=True)
    if args.phase == "build":
        print("built %d repositories" % len(spec))
        return
    if expect is None:
        with open(args.out, "w") as fh:
            json.dump(results, fh, indent=1)
        print("recorded %d interop checks" % len(results))
        return
    index = {(r["scenario"], r["peer"], r["cmd"]): r for r in expect}
    scored = []
    for r in results:
        if r["cmd"] == "<build>":
            continue
        key = (r["scenario"], r["peer"], r["cmd"])
        want = index.get(key)
        if want is None:
            continue
        # expectations ship as digests; see verify/digestify.py
        try:
            ok = (digest(r["status"], r["output"]) == want["digest"]) if "digest" in want \
                else (want["status"] == r["status"] and want["output"] == r["output"])
        except Exception:  # noqa: BLE001
            ok = False          # one bad reply costs one check, not the run
        scored.append({"scenario": r["scenario"], "peer": r["peer"], "cmd": r["cmd"],
                       "ok": ok, "got": r["output"] if not ok else "",
                       "got_status": r["status"]})
    with open(args.out, "w") as fh:
        json.dump(scored, fh, indent=1)
    print("interop checks: %d/%d" % (sum(1 for s in scored if s["ok"]), len(scored)))


if __name__ == "__main__":
    main()
