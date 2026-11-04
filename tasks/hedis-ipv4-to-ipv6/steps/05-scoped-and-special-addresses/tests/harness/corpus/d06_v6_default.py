"""What a server does when nobody told it what to bind, and what it still does
when somebody did.

Step 6 moves one line: the address `listenToPort()` uses when the
configuration named none. It used to be `NULL`, which with `AI_PASSIVE` means
"the wildcard of every family this host has"; it is now the IPv6 wildcard,
written down. Everything here is that line seen from the outside, plus the
cases that must *not* have moved -- because the cheap way to satisfy "the
default is IPv6" is to make IPv6 the only thing there is, and that is a
different step's requirement and a worse tree.

Four things are asked, in the order the instruction asks them:

  * a server given no `bind` line answers on IPv6 and refuses IPv4, and a
    second process can take the IPv4 wildcard on the same port -- which is the
    part a dual-stack listener could not do, and so is the part that says the
    socket is `AF_INET6` with `IPV6_V6ONLY` set rather than merely "answering
    on IPv6" -- and the same of a server given a `unixsocket` and still no
    `bind` line, which is the one pairing the instruction names as untouched
    and which neither half on its own is;
  * an address an operator wrote is bound as written: a literal of either
    family, the IPv4 wildcard, a mixed list, and -- step 1's requirement, which
    this step must not quietly withdraw -- a *name*, which still resolves to
    every family it has;
  * the configuration this repository ships comes up under the new default,
    with no live `bind` line of its own;
  * and the default is a choice rather than a preference: on a kernel with no
    IPv6, a server with no `bind` line fails and says so, while the same
    kernel's `bind 127.0.0.1` still works.

The last of those is the only one that can tell a default apart from a
fallback, and nothing about this host can ask it: every machine this harness
runs on has IPv6, so "try `::`, and take IPv4 if that fails" and "bind `::`"
are the same tree. `lib/no6.py` is the instrument that takes IPv6 away, and its
known answer is emitted beside the finding.

The throughput requirement is a boolean on purpose. It is a ratio between two
servers measured in the same run on the same host, and the number itself is a
property of the machine and the minute; what the instruction asks is that
serving over IPv6 is not *slower in kind*, and a bound is the only part of that
which means the same thing on a loaded host as on an idle one.
"""

import errno
import os
import re
import socket
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, os.pardir, "lib"))
import ctx as ctxmod  # noqa: E402
import no6  # noqa: E402
import norm  # noqa: E402
import resp  # noqa: E402
import srv  # noqa: E402

PORTS = 6
FAMILY = "behaviour"

FAMILIES = {"v4": socket.AF_INET, "v6": socket.AF_INET6}

# Long enough that nothing here expires on a loaded host. Nothing waits it out:
# `_settle` also returns as soon as the process has gone, which is how the
# cases that are supposed to fail end, and they end in well under a second.
SETTLE = 60.0

LOG_KEYWORDS = ("bind", "listen", "socket", "address", "ready to accept",
                "ipv", "tcp", "warning", "error", "fatal", "aborting")

# Startup warnings about the host kernel rather than about the tree.
HOST_WARNINGS = ("transparent huge pages", "overcommit_memory", "somaxconn",
                 "tcp backlog setting of", "maximum number of open files")


def _log_lines_text(blob):
    out = []
    for line in norm.normalize_text(blob).splitlines():
        low = line.lower()
        if any(w in low for w in HOST_WARNINGS):
            continue
        if any(w in low for w in LOG_KEYWORDS):
            out.append(line)
    return out


# How a log line spells "every IPv6 address" for the address it could not
# bind. This tree writes the bracketed form step 3 settled on; the rest are
# what a tree that reached the same conclusion in its own words would write,
# `:::` among them because that is how the unbracketed spelling comes out once
# the port is joined onto it, as in `listening on :::7502`.
# `::1` is deliberately not among them -- what the cell says is that the
# address the default could not have was the wildcard, and a tree that reports
# the loopback instead has a different default and should read false here,
# which `:::` does not disturb: no spelling of `::1` has three colons in a row.
V6_WILDCARD_TEXT = ("[::]", ":: ", "::0", "[::0]", "0:0:0:0:0:0:0:0", ":::")

# What "this kernel has no such family" reads like. The first is glibc's
# `strerror(EAFNOSUPPORT)`, which is what this tree prints because it prints
# what the C library handed it; the other two are the same fact worded by
# something else. Matched without case, because the wording is not the tree's
# to get right -- only the fact is.
UNSUPPORTED_TEXT = ("address family not supported", "eafnosupport",
                    "not supported by protocol")


def _names_v6_wildcard(lines, port):
    """Whether some line names the IPv6 wildcard, with the port beside it."""
    for line in lines:
        if str(port) not in line:
            continue
        # Padded, so a line that ends at the address still matches `":: "`.
        if any(w in line + " " for w in V6_WILDCARD_TEXT):
            return True
    return False


def _says_unsupported(lines):
    """Whether some line blames the family rather than the address."""
    return any(t in line.lower() for line in lines for t in UNSUPPORTED_TEXT)


def nobind_at(port):
    """`default_directives` with the `bind` line taken out.

    Not `bind ::` and not `bind *`: the requirement is about a configuration
    that does not mention the directive at all, which is a different input to
    the parser and the only one this step changes.
    """
    return [d for d in ctxmod.default_directives(port)
            if not d.startswith("bind ")]


def _connect(host, port, family=socket.AF_UNSPEC, timeout=3.0):
    """A short, stable word for what happened on connect(), as c08 says it."""
    try:
        infos = socket.getaddrinfo(host, port, family, socket.SOCK_STREAM)
    except socket.gaierror as e:
        return "gaierror:%s" % errno.errorcode.get(-e.errno, e.errno)
    if not infos:
        return "no-address"
    af, st, proto, _, sa = infos[0]
    s = socket.socket(af, st, proto)
    s.settimeout(timeout)
    try:
        s.connect(sa)
    except socket.timeout:
        s.close()
        return "timeout"
    except OSError as e:
        s.close()
        return errno.errorcode.get(e.errno, str(e.errno))
    try:
        s.sendall(resp.encode_command(["PING"]))
        data = s.recv(64)
        return "pong" if data.startswith(b"+PONG") else "spoke:%r" % data[:16]
    except OSError as e:
        return "io:%s" % errno.errorcode.get(e.errno, e.errno)
    finally:
        s.close()


def _connect_unix(path, timeout=3.0):
    """The same short word as `_connect`, for a socket with no address in it.

    Nothing about a family can be asked here and nothing needs to be: the cell
    exists to say the unix listener is still served, and the two TCP probes
    recorded beside it are where this step's requirement is.
    """
    try:
        with resp.Client(unixsocket=path, timeout=timeout) as c:
            return "pong" if c.cmd("PING")[1] == b"PONG" else "odd"
    except socket.timeout:
        return "timeout"
    except OSError as e:
        return errno.errorcode.get(e.errno, str(e.errno))


def _reachable(host, port, timeout=5.0):
    try:
        with resp.Client(host, port, timeout=timeout) as c:
            return c.cmd("PING")[1] == b"PONG"
    except Exception:
        return False


def _settle(server, hosts, port, deadline=SETTLE):
    """The first of `hosts` that answers, or None.

    The host and not just the fact is returned because the caller then has to
    talk to the server, and "one of these answered" is not an address. On a
    tree that only has the second host -- the untouched baseline, where a name
    resolves to both families and only the IPv4 listener exists -- asking the
    first one anyway is a refused connection in the middle of a scenario that
    was recording perfectly well.

    Never raises, and never waits on a process that has already gone.
    """
    end = time.time() + deadline
    while time.time() < end:
        for h in hosts:
            if _reachable(h, port, timeout=1.0):
                return h
        if server.proc is not None and server.proc.poll() is not None:
            return None
        time.sleep(0.1)
    return None


def _can_take_v4_wildcard(port):
    """Whether a *different* process can hold `0.0.0.0:port` right now.

    This is the observation that separates "an AF_INET6 socket with
    IPV6_V6ONLY" from "an AF_INET6 socket serving both families": the second
    owns the IPv4 wildcard on that port and nothing else can have it. Bound and
    listened on, not merely bound, because a dual-stack listener collides at
    `bind`, and `SO_REUSEADDR` so that a port left in `TIME_WAIT` by an earlier
    case reads as free -- which it is -- while a listening socket reads as
    taken, which it also is. The same two options `srv.busy_ports` uses.
    """
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    try:
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        s.bind(("0.0.0.0", port))
        s.listen(1)
        return True
    except OSError:
        return False
    finally:
        s.close()


def _conf_from_tree(ctx, rel, port, name, extra=()):
    """The tree's own configuration file, with only where-to-run appended.

    Nothing in the file is edited: redis takes the last occurrence of a
    directive, so appending `port`, `dir` and `logfile` moves the server into
    this scenario's workspace and leaves every other line -- `bind` above all --
    saying exactly what the tree ships it saying. Whether that line is still
    live is the question; rewriting it here would answer it.
    """
    d = ctx.workspace.sub(name)
    try:
        with open(os.path.join(ctx.tree.root, rel), "rb") as f:
            base = f.read().decode("utf-8", "replace")
    except OSError:
        return None, d, None
    conf = os.path.join(d, "%s.conf" % name)
    # (`srv.write_new`: nothing in the score uid's directory is followed.)
    srv.write_new(conf, base.rstrip("\n")
                  + "\n\n# appended by the scorer: where to run, nothing else\n"
                  + "port %d\ndir .\nlogfile server.log\ndaemonize no\n" % port
                  + "".join("%s\n" % line for line in extra))
    return conf, d, base


# Spellings of "every address of this family", as g06 lists them: a `bind` line
# carrying one of these hands the server the same wildcard it gets from no
# `bind` line at all, so it does not count as the shipped file naming a place.
BIND_WILDCARDS = ("*", "::", "[::]", "0.0.0.0", "::0", "0:0:0:0:0:0:0:0")


def _names_somewhere(arg):
    a = arg.strip().lower()
    if not a or a.startswith("#"):
        return False
    if a in BIND_WILDCARDS:
        return False
    t = a[1:].split("]", 1)[0] if a.startswith("[") else a
    try:
        if socket.inet_pton(socket.AF_INET6, t) == b"\x00" * 16:
            return False
    except (OSError, ValueError):
        pass
    return True


def _live_bind(text):
    """Whether this configuration has a live `bind` line that names somewhere."""
    if not text:
        return False
    for line in text.splitlines():
        words = line.split()
        if not words or words[0].lower() != "bind":
            continue
        if any(_names_somewhere(w) for w in words[1:]):
            return True
    return False


def _case(ctx, name, port, directives, ready_hosts, extra_argv=(),
          launcher=(), adv=False, config=("bind",)):
    """Start one server, probe both families from the outside, record, stop."""
    s = ctx.server(name, port, directives, host=ready_hosts[0],
                   extra_argv=extra_argv, launcher=launcher)
    s.start()
    live = _settle(s, ready_hosts, port)
    # Published on the server, because two cells outside this function are
    # about a server started here and have to know whether it is there.
    s.live = live
    ctx.emit("v6def/%s/started" % name, live is not None, adv=adv)
    # A server that never came up says nothing about either family, and
    # "refused" is what a dead port answers as well as what an IPv6-only
    # listener answers on IPv4. `not-started` is deliberately neither `pong`
    # nor the name of an errno, so that no probe cell of any case here can be
    # collected by a server that is not there.
    for label, host in (("v6", "::1"), ("v4", "127.0.0.1")):
        ctx.emit("v6def/%s/probe-%s" % (name, label),
                 _connect(host, port, FAMILIES[label])
                 if live is not None else "not-started", adv=adv)
    if live is not None:
        # `live`, not `ready_hosts[0]`: the reply to CONFIG GET is the same
        # whichever family carried the question, and on a tree that only has
        # one of them this is the difference between recording the answer and
        # failing the scenario.
        with s.client(host=live) as c:
            for param in config:
                ctx.emit_reply("v6def/%s/config-%s" % (name, param),
                               c.cmd("CONFIG", "GET", param), adv=adv)
    ctx.emit("v6def/%s/log" % name, _log_lines_text(s.log()), adv=True)
    return s


# `redis-benchmark -q` writes one line per test: `src/redis-benchmark.c:452`,
# `"%s: %.2f requests per second"`. Both tests are read and the faster is
# taken; which of the two it was does not matter, because the same pair is read
# from both servers.
BENCH_LINE = re.compile(r":\s*([0-9.]+) requests per second")


def _throughput(ctx, host, port, label):
    """Requests per second the tree's own benchmark reports, best of three.

    Best of several rather than a mean: every sample is the same work done on
    the same machine, so a slow one is the machine being busy and the fastest
    is the closest this host gets to measuring the tree. `run_benchmark` is
    already retried past the progress-timer race.

    Three samples and not more because the machine is shared: this scenario
    runs beside three others, and past a point more samples buy a better
    reading of a busier host rather than a better reading of the tree. What is
    scored is not either number -- the pair is emitted only as a note, and the
    cells beside it are the factor-of-two band the instruction states. A
    recorded rate would be an expectation that expires the first time the box
    is busier than it was on the day this was recorded.
    """
    best = 0.0
    for _ in range(3):
        rc, out = srv.run_benchmark(
            ctx.tree, ["-h", host, "-p", str(port), "-n", "20000", "-c", "20",
                       "-t", "set,get", "-q"], timeout=120)
        if rc == "timeout":
            continue
        for m in BENCH_LINE.finditer((out or b"").decode("utf-8", "replace")):
            best = max(best, float(m.group(1)))
    ctx.note("%s throughput best-of-3: %.0f req/s" % (label, best))
    return best


def run(ctx):
    p = [ctx.port(i) for i in range(PORTS)]
    work = ctx.workspace.sub("d06")

    # -- 1. no bind directive at all ---------------------------------------
    nobind = [d for d in ctxmod.default_directives(p[0])
              if not d.startswith("bind ")]
    s0 = _case(ctx, "default", p[0], nobind, ["::1", "127.0.0.1"])
    # The observation the whole step turns on, and its control below. `None`
    # when the server above never came up: the IPv4 wildcard is free on a
    # machine where nothing is listening at all, and that reading is not
    # evidence that this tree left it free.
    ctx.emit("v6def/default/v4-wildcard-free",
             _can_take_v4_wildcard(p[0]) if s0.live is not None else None)
    s0.stop()

    # From argv rather than from a file: `--port` and nothing else. The
    # configuration parser has two entry points and only one of them is a file.
    a0 = ctx.server("argv-default", p[0],
                    ["dir .", "logfile server.log", "daemonize no",
                     'save ""', "appendonly no"],
                    extra_argv=["--port", str(p[0])])
    a0.start()
    # `_case`'s fail-closed probes, spelled out: this server is started here
    # rather than there because `--port` on argv is the other entry point to
    # the configuration parser, and the same rule applies -- a server that
    # never came up says nothing about either family.
    a_live = _settle(a0, ["::1", "127.0.0.1"], p[0])
    ctx.emit("v6def/argv-default/started", a_live is not None)
    ctx.emit("v6def/argv-default/probe-v6",
             _connect("::1", p[0], socket.AF_INET6)
             if a_live is not None else "not-started")
    ctx.emit("v6def/argv-default/probe-v4",
             _connect("127.0.0.1", p[0], socket.AF_INET)
             if a_live is not None else "not-started")
    a0.stop()

    # -- 2. an address an operator wrote is bound as written ---------------
    _case(ctx, "explicit-v4", p[1],
          ctxmod.default_directives(p[1], "127.0.0.1"),
          ["127.0.0.1"]).stop()

    w4 = _case(ctx, "wildcard-v4", p[2],
               ctxmod.default_directives(p[2], "0.0.0.0"), ["127.0.0.1"])
    # The control for `default/v4-wildcard-free`: the same probe, against a
    # server that really does hold the IPv4 wildcard. A probe that answered
    # "free" whatever was listening would say so here.
    ctx.emit("v6def/wildcard-v4/v4-wildcard-free", _can_take_v4_wildcard(p[2]))
    w4.stop()

    _case(ctx, "mixed", p[3],
          ctxmod.default_directives(p[3], "::1 127.0.0.1"),
          ["::1", "127.0.0.1"]).stop()

    # Step 1's requirement, restated here because this step is the one that
    # could take it away: a *name* is not the absence of a directive, and still
    # yields a listener of every family it resolves to.
    _case(ctx, "name-bind", p[4],
          ctxmod.default_directives(p[4], "localhost"),
          ["::1", "127.0.0.1"]).stop()

    # A bind on argv over a file that has none: still bound as written.
    _case(ctx, "argv-bind-v4", p[1], nobind_at(p[1]), ["127.0.0.1"],
          extra_argv=["--bind", "127.0.0.1"]).stop()

    # `unixsocket` and no `bind` is the one pairing the instruction names as
    # untouched, and it is not either half on its own: the unix listener is
    # opened by a different call in a different place, and a tree that decided
    # "a server holding a unix socket has already been told what to listen on"
    # would pass every case above and fail here. All three are asked, because
    # the failure has three shapes -- the TCP listener silently not made, the
    # unix socket silently not made, and the TCP listener made on the old
    # default because this configuration was read as naming something.
    #
    # `p[0]` again: every server this scenario has started on it is stopped.
    # The socket goes under the workspace rather than `/tmp`, so that a second
    # run of this scenario cannot find the first run's, and `norm` masks that
    # directory, so the recorded log line reads `<WORK>/d06/u.sock` wherever
    # the run happened. Named as short as c08 names its own, because the work
    # root is the runner's to choose and `sun_path` is 108 bytes.
    sock = os.path.join(work, "u.sock")
    us = _case(ctx, "unixsocket-nobind", p[0],
               nobind_at(p[0]) + ["unixsocket %s" % sock],
               ["::1", "127.0.0.1"])
    ctx.emit("v6def/unixsocket-nobind/unix", _connect_unix(sock))
    us.stop()

    # -- 3. the configuration this repository ships ------------------------
    conf, d, text = _conf_from_tree(ctx, "redis.conf", p[5], "shipped-conf")
    ctx.emit("v6def/shipped-conf/present", conf is not None)
    ctx.emit("v6def/shipped-conf/live-bind", _live_bind(text))
    if conf is not None:
        s5 = ctx.server("shipped-conf", p[5], [], datadir=d, conf=conf)
        s5.start()
        started = _settle(s5, ["::1", "127.0.0.1"], p[5]) is not None
        ctx.emit("v6def/shipped-conf/started", started)
        ctx.emit("v6def/shipped-conf/probe-v6",
                 _connect("::1", p[5], socket.AF_INET6)
                 if started else "not-started")
        ctx.emit("v6def/shipped-conf/probe-v4",
                 _connect("127.0.0.1", p[5], socket.AF_INET)
                 if started else "not-started")
        ctx.emit("v6def/shipped-conf/log", _log_lines_text(s5.log()), adv=True)
        s5.stop()
    else:
        ctx.emit("v6def/shipped-conf/started", False)
        ctx.emit("v6def/shipped-conf/probe-v6", "no-conf")
        ctx.emit("v6def/shipped-conf/probe-v4", "no-conf")
        ctx.emit("v6def/shipped-conf/log", [], adv=True)

    # Sentinel inherits the same default, and the file it ships inherits the
    # same question about its own `bind` line.
    try:
        with open(os.path.join(ctx.tree.root, "sentinel.conf"), "rb") as f:
            stext = f.read().decode("utf-8", "replace")
    except OSError:
        stext = None
    ctx.emit("v6def/shipped-sentinel/present", stext is not None)
    ctx.emit("v6def/shipped-sentinel/live-bind", _live_bind(stext))

    sd = ctx.workspace.sub("sentinel-default")
    sconf = os.path.join(sd, "sentinel-default.conf")
    srv.write_new(sconf, "port %d\ndir .\nlogfile sentinel.log\ndaemonize no\n"
                  "sentinel monitor mymaster ::1 %d 1\n"
                  "sentinel down-after-milliseconds mymaster 60000\n"
                  % (p[5], p[0]))
    sen = ctx.server("sentinel-default", p[5], [], binary="redis-sentinel",
                     datadir=sd, conf=sconf)
    sen.start()
    sen_live = _settle(sen, ["::1", "127.0.0.1"], p[5])
    ctx.emit("v6def/sentinel-default/started", sen_live is not None)
    ctx.emit("v6def/sentinel-default/probe-v6",
             _connect("::1", p[5], socket.AF_INET6)
             if sen_live is not None else "not-started")
    ctx.emit("v6def/sentinel-default/probe-v4",
             _connect("127.0.0.1", p[5], socket.AF_INET)
             if sen_live is not None else "not-started")
    sen.stop()

    # -- 4. a choice of family, not a preference to fall back from ---------
    for key, value in sorted(no6.check(work).items()):
        ctx.emit("v6def/no-v6/instrument/%s" % key, value)
    launch6 = no6.launcher(work)

    rc, out, log = srv.start_server_expect_failure(
        ctx.tree, ctx.workspace, "no-v6-default",
        [d for d in ctxmod.default_directives(p[0]) if not d.startswith("bind ")],
        timeout=20, launcher=launch6)
    ctx.emit("v6def/no-v6/default/rc", rc)
    ctx.emit_text("v6def/no-v6/default/out", out, adv=True)
    failure_log = _log_lines_text(log)
    ctx.emit("v6def/no-v6/default/log", failure_log, adv=True)
    # Two weaker readings of the same captured log, beside the verbatim cell
    # and not instead of it. The verbatim one is kept because `[::]` in it is
    # the only evidence anywhere in this corpus that the address the default
    # asked for was the wildcard as this tree writes it, and because `rc` alone
    # cannot tell a server that failed and said why from one that failed for
    # some other reason. But it also pins glibc's wording of EAFNOSUPPORT, and
    # a tree that failed just as truthfully in its own words would score that
    # cell as though it had not failed at all. These two ask the two facts the
    # instruction asks for and not the sentence: which address it wanted, and
    # what it was about that address that could not be had.
    #
    # Both are conjoined with the fact that it failed, so that what each says
    # is "it failed, *and* it named the address" and "it failed, *and* it said
    # what about the family". The instruction calls the default a choice of
    # family and not a preference to fall back from, and a tree that logged
    # EAFNOSUPPORT and then started on IPv4 anyway would otherwise collect
    # both of these for saying so.
    #
    # Which values of `rc` mean what, because it is not an exit status alone:
    # `start_server_expect_failure` returns the process's status when the
    # process ended inside the deadline, and the *string* `"timeout"` when it
    # was still running when the deadline passed. A server that came up and
    # stayed up is therefore `"timeout"`, not `0`, and `rc != 0` would let the
    # fallback through -- so the test is that `rc` is neither. On the seven
    # recorded trees the two readings agree: `"timeout"` at the pin and at
    # steps 4 and 5, where both cells are false either way because that log
    # says only that the server is ready, and `1` at steps 1, 2, 3 and 6.
    failed = rc not in (0, "timeout")
    ctx.emit("v6def/no-v6/default/log-names-v6-wildcard",
             failed and _names_v6_wildcard(failure_log, p[0]))
    ctx.emit("v6def/no-v6/default/log-says-unsupported",
             failed and _says_unsupported(failure_log))

    # The control, under the same filter: an operator who asked for IPv4 gets
    # it, on a kernel that has nothing else. A filter that broke every server
    # would fail here too, and this is what says it did not.
    _case(ctx, "no-v6-explicit-v4", p[1],
          ctxmod.default_directives(p[1], "127.0.0.1"), ["127.0.0.1"],
          launcher=launch6).stop()

    # -- 5. serving over IPv6 is not slower in kind ------------------------
    v6s = _case(ctx, "parity-v6", p[0], nobind_at(p[0]), ["::1"])
    v6rate = _throughput(ctx, "::1", p[0], "ipv6 default")
    v6s.stop()
    v4s = _case(ctx, "parity-v4", p[1],
                ctxmod.default_directives(p[1], "127.0.0.1"), ["127.0.0.1"])
    v4rate = _throughput(ctx, "127.0.0.1", p[1], "ipv4 explicit")
    v4s.stop()
    # A bound, not a number: the rate is a property of this machine and this
    # minute, and a recorded rate is an expectation that expires. Both halves
    # are emitted so that "the benchmark never ran" cannot read as "within a
    # factor of two".
    #
    # Only one of the two is weighted, and it is the band, because only the
    # band is the requirement. `within-factor-2` is a `regression` cell -- the
    # property was already true of the previous step's tree, and the role is
    # the sync's to compute rather than this file's to choose -- among three
    # thousand other regression observations at `w=1`, so at that weight a
    # tree whose IPv6 path is ten times slower than its IPv4 path would lose
    # about 1/3000 of the regression fraction. At 15 it loses about half a
    # percent of it, and the regression fraction multiplies the whole reward,
    # so the loss is half a percent of everything rather than a rounding error
    # on one term.
    #
    # `both-measured` stays at `w=1`. It is not the requirement, it is the
    # reading of the instrument -- whether the benchmark produced two numbers
    # -- and a benchmark that failed to run fails the band as well, so
    # weighting it too would charge a busy host twice for one fact.
    ctx.emit("v6def/parity/both-measured", v6rate > 0 and v4rate > 0)
    ctx.emit("v6def/parity/within-factor-2",
             bool(v6rate > 0 and v4rate > 0 and v6rate * 2.0 >= v4rate),
             w=15.0)
