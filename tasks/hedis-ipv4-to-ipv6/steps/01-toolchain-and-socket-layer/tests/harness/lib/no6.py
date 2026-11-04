"""A kernel with no IPv6 in it, as something a scenario can put in front of a
process.

Step 6 makes the family a server picks when nothing told it what to bind a
*choice*: `::`, spelled out. An implementation that instead tries IPv6 and
falls back to IPv4 when the bind fails is a different thing, and on every host
this harness runs on the two are indistinguishable, because the bind never
fails. Taking IPv6 away is the only way to ask which one is in the tree.

The instrument is `tools/no_ipv6.c`, the mirror of step 4's `no_ipv4.c`: a
seccomp filter that fails exactly `socket(AF_INET6, ...)` with `EAFNOSUPPORT`,
the errno a kernel built without IPv6 returns. It is compiled at run time
rather than built into the verifier image, in both places this harness runs --
the image ships `gcc` and `libseccomp-dev` for step 4's launcher already, and
compiling is what the recorder does for that one too.

`check()` is the known answer, and it is emitted beside the finding for the
same reason step 4's is: "the launcher was not available" and "the server
refused to start" are both a server that is not running, and only one of them
is the question. The control -- the same call without the launcher -- is what
says the finding is about the filter and not about the box.
"""

import json
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))

# The launcher's source, in `tests/harness/tools`.
SOURCE = os.path.join(HERE, os.pardir, "tools", "no_ipv6.c")

PROBE = """
import errno, json, socket
out = {}
for name in ("AF_INET", "AF_INET6", "AF_UNIX"):
    try:
        s = socket.socket(getattr(socket, name), socket.SOCK_STREAM)
        s.close()
        out[name] = "ok"
    except OSError as e:
        out[name] = errno.errorcode.get(e.errno, str(e.errno))
print(json.dumps(out))
"""


def _tool_dir(workdir):
    """Where a launcher compiled at run time is kept.

    Never `workdir` when this runs as root. A scenario's work directory is
    chowned to the demoted server user (`srv.Workspace.sub`), so an executable
    found there could have been put there by the candidate's own processes --
    and `check()` runs the launcher as root. As root the launcher is compiled
    into a root-owned 0755 directory instead, which the server user can execute
    from but not write to. Unprivileged (the recorder) nothing changes.
    """
    if os.geteuid() != 0:
        return workdir
    d = "/usr/local/lib/hedis-launchers"
    os.makedirs(d, exist_ok=True)
    os.chown(d, 0, 0)
    os.chmod(d, 0o755)
    return d


def ensure(workdir):
    """(path, origin) for the launcher, compiling it into `workdir` if needed."""
    if not os.path.exists(SOURCE):
        return None, "no source at %s" % SOURCE
    out = os.path.join(_tool_dir(workdir), "no-ipv6")
    if os.access(out, os.X_OK):
        return out, "compiled"
    try:
        p = subprocess.run(["gcc", "-O2", "-o", out, SOURCE, "-lseccomp"],
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    except OSError as e:
        # No compiler here at all. Fail closed and say which of the two
        # happened: a launcher that was never built and one that refused to
        # filter are the same missing filter downstream, and only the caller's
        # `launcher-available` cell can tell them apart.
        return None, "no gcc: %s" % e
    if p.returncode != 0 or not os.path.exists(out):
        return None, "gcc rc=%d: %s" % (
            p.returncode, p.stdout.decode("utf-8", "replace").strip()[:200])
    os.chmod(out, 0o755)
    return out, "compiled"


def launcher(workdir):
    """The argv prefix that puts a process under the filter, or ()."""
    path, _ = ensure(workdir)
    return [path] if path else ()


def _families(argv):
    p = subprocess.run(argv + [sys.executable, "-c", PROBE],
                       stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
    try:
        return json.loads(p.stdout.decode("utf-8", "replace").splitlines()[-1])
    except (ValueError, IndexError):
        return {"AF_INET": "<no answer>", "AF_INET6": "<no answer>",
                "AF_UNIX": "<no answer>"}


def check(workdir):
    """The launcher's known answer, as {observation suffix: value}."""
    path, _origin = ensure(workdir)
    out = {"launcher-available": path is not None}
    filtered = _families([path]) if path else {
        k: "<no launcher>" for k in ("AF_INET", "AF_INET6", "AF_UNIX")}
    for fam in ("AF_INET", "AF_INET6", "AF_UNIX"):
        out["filtered/%s" % fam] = filtered[fam]
    out["unfiltered/AF_INET6"] = _families([])["AF_INET6"]
    return out
