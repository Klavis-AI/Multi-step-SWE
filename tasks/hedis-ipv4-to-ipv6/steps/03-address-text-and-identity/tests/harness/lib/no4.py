"""A kernel with no IPv4 in it, as something a scenario can put in front of a
process.

Step 4 asks whether the tree still works where `socket(AF_INET, ...)` cannot
succeed. Nothing about a container gives that: an image with no IPv4 address
configured still has an IPv4 stack, and a tree that asks for an `AF_INET`
socket and is handed one has not been asked the question. So the property is
imposed on the process rather than looked for in the box. `tools/no_ipv4.c`
installs a seccomp filter that fails exactly `socket(AF_INET, ...)` with
`EAFNOSUPPORT` -- the errno a kernel built without `CONFIG_INET` returns -- and
allows every other call, then `execvp`s its argument. A seccomp filter survives
both `fork` and `execve`, so everything downstream of the launched process is
under it too: a `redis-server` that forks a `BGSAVE` child, a `redis-cli
--cluster` that shells out, a Tcl suite that starts fifteen servers.

Not every environment that runs this harness ships the launcher. The verifier
image builds it (`tests/Dockerfile`); a recording environment may not. So the
module uses the binary where there is one and compiles the same source where
there is not, and says which happened -- "the launcher was not available" and
"the launcher found nothing wrong" are the same empty answer otherwise, which
is the shape of a wrong green. Which of the two happened is a note; *that*
there is one is scored.

Nothing here is trusted on its own. `check()` is the launcher's known answer:
the three families that must be unaffected, the one that must fail and the
errno it must fail with, and the same call *without* the launcher, which must
succeed. A scenario emits those beside whatever it went on to measure, so a
filter that stopped working reads as a broken instrument rather than as a tree
that suddenly has no IPv4 in it.
"""

import json
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))

# The launcher's source, in `tests/harness/tools`.
SOURCE = os.path.join(HERE, os.pardir, "tools", "no_ipv4.c")
INSTALLED = "/usr/local/bin/no-ipv4"

# One probe, four questions, no import of anything the tree provides. Printed as
# JSON on stdout so a compiler warning or a linker message on stderr cannot be
# mistaken for an answer.
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
    """(path, origin) for the launcher, compiling it into `workdir` if needed.

    `origin` is 'image' or 'compiled', or the reason there is no launcher.
    """
    if os.access(INSTALLED, os.X_OK):
        return INSTALLED, "image"
    if not os.path.exists(SOURCE):
        return None, "no source at %s" % SOURCE
    out = os.path.join(_tool_dir(workdir), "no-ipv4")
    if os.access(out, os.X_OK):
        return out, "compiled"
    p = subprocess.run(["gcc", "-O2", "-o", out, SOURCE, "-lseccomp"],
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
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
    """The launcher's known answer, as {observation suffix: value}.

    Four facts, and the control is not optional. `filtered/AF_INET` reading
    `EAFNOSUPPORT` says the filter is on; `unfiltered/AF_INET` reading `ok` says
    it is the filter and not the box. A run where both fail has measured a
    machine with no IPv4 configured, which is a different claim and a weaker
    one, and it is the claim a scenario would silently be making if only the
    first were recorded.
    """
    path, _origin = ensure(workdir)
    # Whether there is a launcher, not which of the two images produced it:
    # the verifier ships the binary and the recorder compiles the same source,
    # so the origin is a fact about the runner rather than about the tree, and
    # a scenario that scored it would report a mismatch on every replay.
    out = {"launcher-available": path is not None}
    filtered = _families([path]) if path else {
        k: "<no launcher>" for k in ("AF_INET", "AF_INET6", "AF_UNIX")}
    for fam in ("AF_INET", "AF_INET6", "AF_UNIX"):
        out["filtered/%s" % fam] = filtered[fam]
    out["unfiltered/AF_INET"] = _families([])["AF_INET"]
    return out
