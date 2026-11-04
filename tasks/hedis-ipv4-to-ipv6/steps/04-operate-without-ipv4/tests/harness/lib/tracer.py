"""Running one of the tree's processes under `strace`, without losing the trace.

`strace -f -ttt -e trace=socket,setsockopt -qq -o FILE CMD` answers the only
question a static scan cannot: which address family the process actually handed
to the kernel, and -- because `-ttt` stamps every line with the epoch second it
was printed at -- when it handed it over. Getting the *lines* is easy; getting
all of them is not, and the way it fails is silent.

`strace` is the parent of the process it traces, so the obvious way to stop a
traced server -- signal the process you started -- signals the tracer. The
tracer detaches and exits without writing out what it had buffered, which for a
trace of a dozen lines is the whole trace, and the scenario then reports "no
AF_INET6 socket" for a server that opened one. Worse, the tracee it detached
from keeps running: it survives the scenario, holds the port, and the *next*
run finds a server already listening and records that one's behaviour instead.
That is the failure this module exists to prevent.

So: signal the tracee, wait for the tracer to finish on its own, and kill
anything still standing afterwards. `failures()` reports traces that came back
without a single socket call, so a scenario can score the instrument alongside
what the instrument measured.

`-ttt` is what turns a count into a duration. A Connection Attempt Delay is the
gap between two consecutive `connect()` calls of *one* process, and read here
that gap carries no process-startup term, no fork and no exec -- which is why it
can be bounded to within a factor of two where a wall-clock reading of a whole
race from outside cannot. The stamp is printed after the `-f` pid prefix and
before the call:

    122959 1789600649.508255 close(3)      = 0

Both halves of that prefix are optional -- a line can carry neither -- so every
regex below takes the stamp as an optional **non-capturing** group. The callers
of these patterns index their groups positionally, and a trace collected by an
older launcher has no stamps at all, so neither the group numbers nor the
patterns' ability to match an unstamped line may move.
"""

import os
import re
import signal
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

SOCKET = re.compile(r"socket\((AF_[A-Z0-9_]+)")
V6ONLY = re.compile(r"setsockopt\(\d+, SOL_IPV6, IPV6_V6ONLY, \[1\]")
SETSOCKOPT = re.compile(r"setsockopt\(\d+, (\w+), (\w+), \[(-?\d+)\]")

# The stamp `-ttt` puts between the pid prefix and the call, as a *capturing*
# group, for the two functions that need to read it. Nothing else may capture
# it: the patterns below are indexed by group number.
#
# The optional pid group in front of it is what makes this safe on an unstamped
# line. `(?:\d+\s+)?` cannot swallow `1789600649.508255` -- there is no
# whitespace after the digits, only a `.` -- and it cannot leave `122959 close`
# looking like a stamp, because `\d+\.\d+` needs the dot. So a line with a pid
# and no stamp reads as no stamp rather than as a stamp of the pid.
_STAMP = re.compile(r"^(?:\d+\s+)?(\d+\.\d+)\s")

# The same three calls with their descriptors, which is what turns "IPV6_V6ONLY
# appears somewhere in this trace" into "this listening socket was restricted
# before it was bound". `-f` prefixes every line with the pid it came from, and
# a descriptor number only means anything inside one process, so the key is the
# pair. A line with no pid prefix is the traced process itself.
_SOCKET_RET = re.compile(r"^(?:(\d+)\s+)?(?:\d+\.\d+\s+)?"
                         r"socket\((AF_[A-Z0-9_]+),[^=]*=\s*(\d+)\s*$")
_V6ONLY_FD = re.compile(r"^(?:(\d+)\s+)?(?:\d+\.\d+\s+)?"
                        r"setsockopt\((\d+), SOL_IPV6, IPV6_V6ONLY, \[1\]")
# Only a bind that returned 0. A socket whose bind failed is not a
# listener, and counting it as one would let a tree that opened the right
# socket and then failed to bind it score the same as a tree that is
# listening -- which is exactly what a stray server on the port produces.
_BIND_FD = re.compile(r"^(?:(\d+)\s+)?(?:\d+\.\d+\s+)?bind\((\d+),.*=\s*0\s*$")


# Where a trace is really written when the verifier runs as root, keyed by the
# path the scenario asked for. The scenario's own directory belongs to the uid
# the traced process runs as, and so did the tracer and its trace: the process
# being judged could rewrite the file it is judged by, or the tracer writing it.
# So as root the tracer stays root and writes into a directory only root can
# enter, created fresh by `mkdtemp`, and `read()` reads from there. The traced
# command is demoted *inside* the trace, by `DEMOTE` exec'ing it: `strace -u`
# demotes before the tracer attaches, and attaching to another uid's process
# needs CAP_SYS_PTRACE, which a verifier container does not have; a child that
# is still root when attached and demotes itself afterwards stays traced. The
# demoting step is an isolated interpreter that sets numeric ids and execs the
# command in the same process, so it adds no line to any trace: `setpriv` was
# tried first and its user lookups open AF_UNIX sockets, which would also keep
# `failures()` from ever seeing an empty trace.
DEMOTE = ["python3", "-I", "-S", "-c",
          "import os, sys; os.setgroups([]); os.setgid(65534); "
          "os.setuid(65534); os.execv(sys.argv[1], sys.argv[1:])"]
_PRIVATE = {"dir": None, "paths": {}}


def _private(path):
    path = os.path.abspath(path)
    paths = _PRIVATE["paths"]
    if path not in paths:
        if _PRIVATE["dir"] is None:
            _PRIVATE["dir"] = tempfile.mkdtemp(prefix="hedis-trace-")
        paths[path] = os.path.join(_PRIVATE["dir"], "%d.txt" % len(paths))
    return paths[path]


def demotes_itself(argv):
    """Whether `argv` is a launcher from `launcher()` that demotes its command."""
    argv = list(argv or ())
    n = len(DEMOTE)
    return (len(argv) > n and os.path.basename(argv[0]) == "strace"
            and argv[-n:] == DEMOTE)


def launcher(path, calls="socket,setsockopt"):
    """The argv prefix that traces a process into `path`.

    `-ttt` is on for every traced process, not only the ones whose timing is
    read: a trace is collected once and asked several questions, and a scenario
    that wanted a duration from a trace collected without stamps would have no
    way to get one afterwards. It costs one field per line and moves no
    observation -- every pattern in this module takes the stamp as optional.

    Run as root, the tracer is not demoted with the command (see `_PRIVATE`
    and `srv.preexec_for`): `DEMOTE` runs only the traced command as the score
    uid, and the trace goes where that uid cannot reach.
    """
    if os.geteuid() == 0:
        return (["strace", "-f", "-ttt", "-e", "trace=" + calls, "-qq",
                 "-o", _private(path)] + DEMOTE)
    return ["strace", "-f", "-ttt", "-e", "trace=" + calls, "-qq", "-o", path]


def children(pid):
    """The direct children of `pid`."""
    try:
        with open("/proc/%d/task/%d/children" % (pid, pid)) as f:
            return [int(x) for x in f.read().split()]
    except (OSError, ValueError):
        return []


def start(s, deadline=5.0):
    """Start a traced server and remember which process the tracer is on.

    The pid is captured now rather than looked up at stop time, because by stop
    time a tracer that already gave up has no children to report.
    """
    s.start()
    s.tracee = None
    end = time.time() + deadline
    while time.time() < end:
        kids = children(s.proc.pid)
        if kids:
            s.tracee = kids[0]
            break
        time.sleep(0.01)
    return s


def stop(s, deadline=15.0):
    """Stop a traced server so that its trace file is complete."""
    if s.proc is None:
        return None
    tracee = getattr(s, "tracee", None)
    if tracee:
        try:
            os.kill(tracee, signal.SIGTERM)
        except OSError:
            pass
        end = time.time() + deadline
        while time.time() < end and s.proc.poll() is None:
            time.sleep(0.02)
    # Whatever the tracer did or did not do, `Server.stop` kills the tracee it
    # was told about here, so the traced process does not outlive the scenario.
    return s.stop()


def read(path):
    # A trace written as root is read only from where root wrote it: the
    # scenario's own path is the score uid's to write, so nothing there is
    # evidence.
    path = _PRIVATE["paths"].get(os.path.abspath(path), path)
    try:
        with open(path, "r", errors="replace") as f:
            return f.read()
    except OSError:
        return ""


def families(text):
    """The address families this trace opened a socket for."""
    return set(SOCKET.findall(text))


def v6only(text):
    """Whether IPV6_V6ONLY was turned on anywhere in this trace."""
    return bool(V6ONLY.search(text))


def v6_listeners(text):
    """(IPv6 sockets bound, how many of those were IPV6_V6ONLY first).

    `v6only()` above answers "did this process ever set the option", which a
    process can do on a socket it then throws away. The question the step
    actually asks is about the sockets it *listens* on: every IPv6 listening
    socket is restricted to IPv6 before it is bound. So the fds an
    `socket(AF_INET6, ...)` returned are followed to their `bind()`, and the
    option has to have been set on that fd in between.

    Needs `bind` in the traced call set; a trace collected without it reports
    no listeners at all, which is why the count is scored rather than a ratio.
    """
    family = {}
    restricted = set()
    bound = v6only_bound = 0
    for line in text.splitlines():
        line = line.strip()
        m = _SOCKET_RET.match(line)
        if m:
            key = (m.group(1), int(m.group(3)))
            family[key] = m.group(2)
            # A descriptor number is reused after a close, and close is not in
            # the traced set: the newest socket() on an fd is the one that owns
            # it, so both facts about the previous owner are dropped here.
            restricted.discard(key)
            continue
        m = _V6ONLY_FD.match(line)
        if m:
            restricted.add((m.group(1), int(m.group(2))))
            continue
        m = _BIND_FD.match(line)
        if m:
            key = (m.group(1), int(m.group(2)))
            if family.get(key) == "AF_INET6":
                bound += 1
                v6only_bound += key in restricted
    return bound, v6only_bound


def failures(traces):
    """The labels whose trace holds no socket call at all.

    A tracer that produced nothing looks exactly like a program that opened
    nothing, so this is scored rather than logged.
    """
    return sorted(k for k, v in traces.items() if not families(v))


# ---------------------------------------------------------------------------
# Connection attempts. `families()` above answers "which families did this
# process ask the kernel for", which is step 1's question. Step 2's question is
# about the attempts themselves: in what order they were made, and how many of
# them were running at the same time. Both are facts about `connect()` lines and
# neither can be read off a socket count.
#
# Needs `connect` in the traced call set, and `close` as well for
# `max_in_flight`; a trace collected without them reports no attempts, which is
# why the counts are scored rather than a ratio.
# ---------------------------------------------------------------------------

_CONNECT = re.compile(
    r"^(?:(?P<pid>\d+)\s+)?(?:\d+\.\d+\s+)?connect\((?P<fd>\d+), "
    r"\{sa_family=(?P<fam>AF_[A-Z0-9_]+)(?P<sa>[^}]*)\},"
    r"\s*\d+\)(?:\s*=\s*(?P<ret>-?\d+)(?:\s+(?P<err>\w+))?|\s*<unfinished)")
_CLOSE = re.compile(r"^(?:(\d+)\s+)?(?:\d+\.\d+\s+)?close\((\d+)\)")
_SA_PORT = re.compile(r"sin6?_port=htons\((\d+)\)")
_SA_ADDR = re.compile(r'inet_pton\(AF_INET6, "([^"]+)"|sin_addr=inet_addr\("([^"]+)"\)')
# The socket type as well as the family, because not every `connect()` to the
# destination port is a connection attempt. Resolving a name with more than one
# address makes glibc sort the answer per RFC 3484, and it finds the source
# address each candidate would be reached from by `connect()`-ing a *datagram*
# socket to it -- same address, same port, no packet on the wire. Six of those
# land in the trace before the tree has made a single attempt, in the resolver's
# order rather than the tree's, so a reader that counted them would report every
# tree as walking the list unsorted and none of them as racing.
_SOCKET_KIND = re.compile(r"^(?:(\d+)\s+)?(?:\d+\.\d+\s+)?"
                          r"socket\((AF_[A-Z0-9_]+), "
                          r"(SOCK_[A-Z0-9_]+)[^=]*=\s*(\d+)\s*$")


def _stamp(line):
    """The epoch second `-ttt` printed this line at, or None."""
    m = _STAMP.match(line)
    return float(m.group(1)) if m else None


def connects(text, port=None, kind="SOCK_STREAM"):
    """Every `connect()` in this trace, in the order strace printed it.

    Each entry is `{pid, fd, family, addr, port, err, t}`. `err` is the errno
    name strace printed, `""` for a call that returned 0, and `"UNFINISHED"` for
    one the process was still inside when the tracer detached -- which is what an
    attempt against a blackholed address looks like from here, and therefore the
    one value that must not be dropped. `t` is the epoch second `-ttt` stamped
    the line with, as a float, and `None` for a trace collected without it.

    `kind` keeps the datagram probes out, and it is followed in line order
    rather than looked up at the end: glibc closes the probe socket and the
    tree's first attempt is handed the same descriptor number back, so which
    kind an fd is depends on where in the trace the question is asked. Passing
    `kind=None` reports every connect whatever it was made on.

    A blocking `connect()` is printed when it *returns*, so for a caller that
    does not race, the order of these lines is the order its attempts finished;
    for one that does, every attempt returns `EINPROGRESS` at once and the order
    is the order they were started. That difference is the subject rather than a
    complication: the two shapes are what `max_in_flight` tells apart.
    """
    out = []
    kinds = {}
    for line in text.splitlines():
        line = line.strip()
        m = _SOCKET_KIND.match(line)
        if m:
            kinds[(m.group(1), int(m.group(4)))] = m.group(3)
            continue
        m = _CONNECT.match(line)
        if not m:
            continue
        key = (m.group("pid"), int(m.group("fd")))
        # An fd whose socket() is not in the trace is reported rather than
        # dropped: `socket` missing from the traced call set must not quietly
        # turn every attempt into no attempt at all.
        if kind is not None and kinds.get(key, kind) != kind:
            continue
        sa = m.group("sa")
        pm = _SA_PORT.search(sa)
        p = int(pm.group(1)) if pm else None
        if port is not None and p != port:
            continue
        am = _SA_ADDR.search(sa)
        out.append({
            "pid": m.group("pid"),
            "fd": key[1],
            "family": m.group("fam"),
            "addr": (am.group(1) or am.group(2)) if am else "?",
            "port": p,
            "err": m.group("err") or ("UNFINISHED" if m.group("ret") is None
                                      else ""),
            "t": _stamp(line),
        })
    return out


def attempts(text, port):
    """The ordered destinations of the connection attempts made to `port`.

    `"AF_INET6/::1"`, as strace rendered the sockaddr the tree handed the
    kernel. The text is `inet_ntop`'s and not the tree's, so it says nothing
    about how the tree *spells* an address and does not move when step 3
    changes that -- which is what lets this gate be scored at every step from
    here on.
    """
    return ["%s/%s" % (c["family"], c["addr"]) for c in connects(text, port)]


def max_in_flight(text, port):
    """The most attempts to `port` outstanding at any one moment.

    An attempt is outstanding from the `connect()` that returned `EINPROGRESS`
    until the descriptor is closed. A call that returned 0 or an error was over
    before the line was printed and never counts, so a caller that connects to
    one address at a time reports 1 however many addresses it walks, and only a
    caller that starts an attempt while another is still running reports more.
    That is the whole of "raced" as opposed to "walked quickly".
    """
    live = set()
    peak = 0
    kinds = {}
    for line in text.splitlines():
        line = line.strip()
        m = _SOCKET_KIND.match(line)
        if m:
            key = (m.group(1), int(m.group(4)))
            kinds[key] = m.group(3)
            # A descriptor reused by a new socket() belonged to an attempt that
            # ended without a close line in the traced set.
            live.discard(key)
            continue
        m = _CONNECT.match(line)
        if m:
            key = (m.group("pid"), int(m.group("fd")))
            if kinds.get(key, "SOCK_STREAM") != "SOCK_STREAM":
                continue
            pm = _SA_PORT.search(m.group("sa"))
            if (int(pm.group(1)) if pm else None) != port:
                continue
            if m.group("err") == "EINPROGRESS" or m.group("ret") is None:
                live.add(key)
                peak = max(peak, len(live))
            continue
        m = _CLOSE.match(line)
        if m:
            live.discard((m.group(1), int(m.group(2))))
    return peak


# ---------------------------------------------------------------------------
# Durations. `max_in_flight` above says attempts overlapped; these say by how
# much they were separated, which is the only way to tell a race that honours a
# delay between attempts from one that starts everything at once. Both read the
# `-ttt` stamp and both are numbers: what a given number *means* is a question
# about a requirement, so the scenario that asks it does the bucketing.
#
# Both return "no reading" rather than raising when the stamps are not there.
# A trace collected by an older launcher has none at all, and a reader that
# raised on it would turn a missing option into a broken scenario instead of an
# unanswered question.
# ---------------------------------------------------------------------------


# What `connects()` reports for an attempt the kernel did not resolve inside the
# call: the errno of a non-blocking attempt in progress, and the word this module
# uses for a line whose return the tracer never saw. Necessary for an attempt
# that is still pending and not sufficient -- see `delay_gaps`.
PENDING = ("EINPROGRESS", "UNFINISHED")


def start_gaps(text, port, kind="SOCK_STREAM"):
    """The seconds between consecutive `connect()` calls to `port`.

    One entry per pair, in trace order, so `n` attempts give `n - 1` gaps and
    fewer than two attempts give none. These are gaps between the *starts* of
    attempts, which for a racing caller is what the Connection Attempt Delay
    names; for a caller that connects blockingly the same subtraction measures
    how long each attempt took to fail, which is a different quantity, so the
    reading is only meaningful beside a `max_in_flight` that says the attempts
    overlapped.

    Empty if any of the lines carried no stamp: a partial reading of a duration
    is worse than no reading, because it is indistinguishable from a real one.
    """
    ts = [c["t"] for c in connects(text, port, kind)]
    if len(ts) < 2 or any(t is None for t in ts):
        return []
    return [b - a for a, b in zip(ts, ts[1:])]


def delay_gaps(text, port, after, kind="SOCK_STREAM"):
    """The entries of `start_gaps` that follow an attempt to `after`.

    An attempt postpones its successor only while it is *still running*: the
    Connection Attempt Delay is what a pending attempt charges, and an attempt
    that has already failed releases the next address at once. So a gap is a
    reading of the timer exactly when the attempt it follows was still
    outstanding when the next one started -- and `after` is how the caller says
    which those are: the addresses it has darkened itself, whose attempts cannot
    complete for as long as the blackhole is up.

    It has to be named rather than inferred. The return value of the `connect()`
    does not answer it: a non-blocking attempt to an *unbound* address also
    returns `EINPROGRESS` and then fails a fraction of a millisecond later, from
    the reset, so a reader that took every `EINPROGRESS` for a pending attempt
    would count the gap in front of that address's successor as a delay reading
    and see ~0, which is what such a reader measures on the golden tree.

    Empty when a stamp is missing, as `start_gaps` is, and when the blackhole
    was not up: an attempt to a darkened address that *connected* is dropped by
    the same test, so a broken instrument reads as no measurement rather than as
    a measurement of zero.

    `start_gaps` remains the reading for "was a delay charged at all"; this one
    is for pinning its value.
    """
    cs = connects(text, port, kind)
    if any(c["t"] is None for c in cs):
        return []
    after = set(after)
    return [b["t"] - a["t"] for a, b in zip(cs, cs[1:])
            if a["addr"] in after and a["err"] in PENDING]


def grace_gap(text, port, kind="SOCK_STREAM"):
    """Seconds from the last attempt to `port` to the first close of one.

    "One" being any descriptor this trace made an attempt to `port` on. A caller
    that does not wait for its connection gets a descriptor back while the race
    is still running, and what it does with the attempts it is not returning is
    invisible from outside: the process has already moved on. From in here it is
    one subtraction -- the last `connect()` starts the final attempt, and the
    close that follows it is the race being wound up.

    The closes are reset at every new attempt, so this is the first close *after
    the last start* rather than the first close in the trace; a race whose early
    attempts fail and are reaped as they go would otherwise report the gap
    between two events that did not bracket the wait at all. None if there is no
    such close -- a caller that hands back an in-flight descriptor and abandons
    the rest never closes anything -- or if either line carried no stamp.

    Needs `close` in the traced call set as well as `socket` and `connect`.
    """
    kinds = {}
    attempted = set()
    last_start = None
    first_close = None
    for line in text.splitlines():
        line = line.strip()
        m = _SOCKET_KIND.match(line)
        if m:
            key = (m.group(1), int(m.group(4)))
            kinds[key] = m.group(3)
            # The same descriptor handed back to a new socket is not the
            # attempt's any more, so a close of it says nothing about the race.
            attempted.discard(key)
            continue
        m = _CONNECT.match(line)
        if m:
            key = (m.group("pid"), int(m.group("fd")))
            if kind is not None and kinds.get(key, kind) != kind:
                continue
            pm = _SA_PORT.search(m.group("sa"))
            if (int(pm.group(1)) if pm else None) != port:
                continue
            attempted.add(key)
            last_start = _stamp(line)
            first_close = None
            continue
        m = _CLOSE.match(line)
        if m and first_close is None:
            if (m.group(1), int(m.group(2))) in attempted:
                first_close = _stamp(line)
    if last_start is None or first_close is None:
        return None
    return first_close - last_start
