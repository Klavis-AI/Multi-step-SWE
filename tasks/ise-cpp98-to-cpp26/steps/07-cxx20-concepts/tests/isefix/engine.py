"""Runs one behaviour session against a built ise tree.

A *session* is one freshly started program plus an ordered list of
interactions with it. Recording and verifying execute the identical code
path here, so an expectation can only ever be what the baseline actually
did.

Every read is bounded by a declared stop condition -- a byte count or the
peer closing -- never by "wait and see what arrived". A time-bounded read
would make the transcript a function of the machine's load, and this VM is
shared.
"""
import base64
import errno
import os
import re
import shutil
import signal
import socket
import subprocess
import threading
import time

from .normalize import (RE_THREAD_FIELD, normalize_text, normalize_wire,
                        narrow_http, seed_thread_map)

HOST = "127.0.0.1"
CONNECT_TIMEOUT = 8.0
DEFAULT_READ_TIMEOUT = 8.0
STARTUP_TIMEOUT = 20.0
SHUTDOWN_GRACE = 15.0


class SessionError(Exception):
    pass


# --------------------------------------------------------------------------
# socket primitives


def _read_until(sock, until, timeout):
    """Read according to a declared stop condition.

    until = {"bytes": N}  -- stop once N bytes have arrived (or peer closes)
    until = {"close": 1}  -- stop when the peer closes
    Returns (data, how) where how is "count", "close" or "timeout".
    """
    want = until.get("bytes")
    deadline = time.time() + timeout
    out = b""
    sock.settimeout(0.25)
    while True:
        if want is not None and len(out) >= want:
            return out[:want], "count"
        if time.time() > deadline:
            return out, "timeout"
        try:
            chunk = sock.recv(65536 if want is None else min(65536, want - len(out)))
        except socket.timeout:
            continue
        except OSError as exc:
            if exc.errno in (errno.ECONNRESET, errno.ENOTCONN):
                return out, "reset"
            raise
        if not chunk:
            return out, "close"
        out += chunk


def do_tcp(step):
    """One TCP connection carrying one or more send/read exchanges."""
    port = step["port"]
    parts = []
    try:
        sock = socket.create_connection((HOST, port), CONNECT_TIMEOUT)
    except OSError as exc:
        return "<connect-failed:%s>" % exc.strerror
    # A step that closes as soon as it connects races the server's own
    # accept-side callback; see catalog.settle for what that costs and which
    # steps ask for this.
    if step.get("settle"):
        time.sleep(step["settle"])
    try:
        for ex in step["exchanges"]:
            if "send_b64" in ex:
                payload = base64.b64decode(ex["send_b64"])
                try:
                    sock.sendall(payload)
                except OSError as exc:
                    parts.append("<send-failed:%s>" % exc.strerror)
                    break
            if ex.get("shutdown_write"):
                # The payload and the FIN behind it arrive in the same breath,
                # and which one ise's event loop sees first decides whether the
                # server ever reads the bytes at all; see catalog.SEND_HOLD for
                # the measurement and which steps ask for the pause.
                if ex.get("hold"):
                    time.sleep(ex["hold"])
                try:
                    sock.shutdown(socket.SHUT_WR)
                except OSError:
                    pass
            until = ex.get("until")
            if until is None:
                continue
            data, how = _read_until(sock, until, ex.get("timeout", DEFAULT_READ_TIMEOUT))
            if ex.get("http"):
                data = narrow_http(data)
            parts.append("<<%s %d>>%s" % (how, len(data), data.decode("latin-1")))
    finally:
        sock.close()
    return "\n".join(parts)


def do_udp(step):
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(step.get("timeout", 2.0))
    try:
        sock.sendto(base64.b64decode(step["send_b64"]), (HOST, step["port"]))
        replies = []
        for _ in range(step.get("expect_datagrams", 1)):
            try:
                data, _addr = sock.recvfrom(65536)
            except socket.timeout:
                replies.append("<no-reply>")
                break
            replies.append("<<dgram %d>>%s" % (len(data), data.decode("latin-1")))
        return "\n".join(replies)
    finally:
        sock.close()


def do_burst(step):
    """Open N connections at once, exchange on each, then close them all.

    Scored on the multiset of transcripts, not their order: the server
    interleaves N connections across its worker threads, so the order in
    which they complete is not a property of the program.
    """
    port, count = step["port"], step["count"]
    socks = []
    for _ in range(count):
        try:
            socks.append(socket.create_connection((HOST, port), CONNECT_TIMEOUT))
        except OSError as exc:
            return "<connect-failed:%s>" % exc.strerror
    seen = []
    try:
        payload = base64.b64decode(step.get("send_b64", ""))
        for sock in socks:
            if payload:
                try:
                    sock.sendall(payload)
                except OSError:
                    pass
        for sock in socks:
            data, how = _read_until(sock, step["until"], step.get("timeout", DEFAULT_READ_TIMEOUT))
            seen.append("<<%s %d>>%s" % (how, len(data), data.decode("latin-1")))
    finally:
        for sock in socks:
            sock.close()
    return "\n".join(sorted(seen))


def do_ladder(step):
    """Open N connections one at a time, each fully served before the next.

    A burst cannot answer a question about admission. ise accepts every
    pending connection before it runs any of their callbacks, so when five
    arrive together all five callbacks read a connection count that already
    includes all five, and how many the server admits stops being a property
    of the server: measured on examples/max_connection, a burst of five
    against a cap of three produced five refusals, a count that read 5 and
    then 6 inside one callback, and a first connection refused before any
    other existed. One connection at a time, each read to its stop condition
    before the next is opened and none of them closed until the end, and the
    counts are 1..N in order -- which is the thing the program decides on.

    The transcripts are therefore returned in order and not as a multiset.
    """
    port, count = step["port"], step["count"]
    payload = base64.b64decode(step.get("send_b64", ""))
    socks, seen = [], []
    try:
        for index in range(count):
            try:
                sock = socket.create_connection((HOST, port), CONNECT_TIMEOUT)
            except OSError as exc:
                seen.append("<<connect-failed %d>>%s" % (index, exc.strerror))
                continue
            socks.append(sock)
            if payload:
                try:
                    sock.sendall(payload)
                except OSError:
                    pass
            data, how = _read_until(sock, step["until"],
                                    step.get("timeout", DEFAULT_READ_TIMEOUT))
            seen.append("<<%s %d>>%s" % (how, len(data), data.decode("latin-1")))
    finally:
        for sock in socks:
            sock.close()
    return "\n".join(seen)


OPS = {"tcp": do_tcp, "udp": do_udp, "burst": do_burst, "ladder": do_ladder}


# --------------------------------------------------------------------------
# per-item transforms
#
# Each one names a specific thing the program does not promise. "sort" is for
# output produced by several threads that have no ordering between them;
# "unique" additionally drops a repeat count the program never fixed; "digits"
# is for live process statistics. Anything not flagged is scored byte for byte.


RE_TRANSCRIPT_LEN = re.compile(r"<<(close|count|timeout|reset|dgram) \d+>>")
RE_CONTENT_LENGTH = re.compile(r"Content-Length: \d+")


def _unmerge(lines):
    """Drop lines that are two other lines run together by interleaved writes.

    `thread_pool` writes to stdout from ten threads with nothing serialising
    them, so now and then one thread's record lands inside another's before the
    newline and the transcript grows a line like

        executing task: N. (thread: TN)executing task: N. (thread: TN)

    Under `unique` that merge is a *new distinct line*, and the item is a
    lottery: it was stable across three passes on one recording and dropped on
    the next. Nothing about the program changed between them.

    The rule is content-free, which is what makes it safe to apply everywhere:
    a line that is exactly the juxtaposition of other lines of the same
    transcript carries no format the transcript does not already show, so it is
    an artifact of how the bytes reached the pipe, not of what the program
    printed. A genuinely new format cannot be spelled out of the others.

    `unique` has already collapsed multiplicity, so this loses nothing else:
    the surviving set is exactly the distinct record formats, which is what the
    item asserts.
    """
    kept = set(lines)
    for line in lines:
        if not line or line not in kept:
            continue
        # Can `line` be spelled as a concatenation of two-or-more other kept
        # lines? Greedy is wrong here -- the records share prefixes -- so this
        # is the ordinary reachability scan over split points.
        others = kept - {line}
        reach = [False] * (len(line) + 1)
        reach[0] = True
        for end in range(1, len(line) + 1):
            for other in others:
                start = end - len(other)
                if start >= 0 and reach[start] and line.startswith(other, start):
                    reach[end] = True
                    break
        if reach[len(line)]:
            kept.discard(line)
    return sorted(kept)


def _http_volatile(text):
    """Collapse the numbers in an HTTP response whose body reports live state.

    The status line, every header name and the body's shape stay exact; only
    the digits inside the body -- and the Content-Length that follows from
    them -- are erased. A blanket digit collapse would take "200 OK" with it,
    which is the one number these fixtures most need to pin.
    """
    text = RE_TRANSCRIPT_LEN.sub(lambda m: "<<%s N>>" % m.group(1), text)
    head, sep, body = text.partition("\r\n\r\n")
    if not sep:
        return text
    head = RE_CONTENT_LENGTH.sub("Content-Length: N", head)
    return head + sep + re.sub(r"\d+", "N", body)


# The fields of /proc/self/status that every Linux since 2.6 reports, in the
# order the kernel emits them. Everything else there is a property of the host
# rather than of the program under test: Speculation_Store_Bypass and
# SpeculationIndirectBranch name this CPU's mitigations, Cpus_allowed and
# Mems_allowed are this machine's masks, and CoreDumping, THP_enabled,
# NStgid..NSsid, Seccomp_filters and HugetlbPages each appeared in some kernel
# version and not the one before it. A recording that keeps them is an
# expectation about the box it was made on, so it would fail on the next one.
PROC_STATUS_FIELDS = (
    "Name", "State", "Tgid", "Pid", "PPid", "TracerPid", "Uid", "Gid",
    "FDSize", "VmPeak", "VmSize", "VmRSS", "VmData", "VmStk", "VmExe",
    "VmLib", "Threads", "SigQ",
    "voluntary_ctxt_switches", "nonvoluntary_ctxt_switches",
)

# Two or more spaces. /proc/self/status right-aligns every Vm* value in a fixed
# column, so the padding is a function of how many digits the value had -- the
# digits http_volatile has just replaced with a single N. Left alone it smuggles
# the value back in: a process whose RSS crosses 10000 kB changes "    N kB" to
# "     N kB" and the item that looked collapsed is not.
RE_ALIGN_PAD = re.compile(r" {2,}")


def _proc_status_body(body):
    """Keep the long-standing fields of /proc/self/status and drop the rest.

    What this fixture is for is that the inspector reads the whole file and
    writes it through untouched -- which the 15-character truncation the kernel
    applies to Name: shows, and which keeping both the first field and the last
    one pins. The values are already gone; the ones kept here are kept for their
    names, their order and their shape.
    """
    kept = []
    for line in body.split("\n"):
        field = line.split(":", 1)[0]
        if field in PROC_STATUS_FIELDS:
            kept.append(RE_ALIGN_PAD.sub(" ", line))
    return "\n".join(kept) + "\n"


def _apply_norm(text, threads, norm):
    norm = norm or {}
    if norm.get("thread_count"):
        # The ids the program *names*, and only those. Counting the session's
        # whole thread map instead -- which is what this did -- counts two
        # different id namespaces and calls the sum a property of the program:
        # `Logger::writeStr` stamps each line with `pthread_self()` truncated
        # to UINT, while `Thread::getThreadId()` is `syscall(SYS_gettid)`, so
        # one pool thread appears in a session under two unrelated numbers.
        # The old expectation was 21 for a pool of ten -- ten tids, ten pthread
        # handles, and the main thread's handle on the "Exit Program" line --
        # and it also silently depended on the log being readable at all, which
        # is how a permission fault in the verifier showed up here rather than
        # in the log item it actually broke. What the pool promises is that ten
        # threads run the task; that is what is scored.
        return "distinct-threads=%d" % len(set(RE_THREAD_FIELD.findall(text)))
    text = normalize_text(text, threads, norm.get("digits", False))
    if norm.get("http_volatile"):
        text = _http_volatile(text)
    if norm.get("proc_status"):
        head, sep, body = text.partition("\r\n\r\n")
        if sep:
            text = head + sep + _proc_status_body(body)
    if norm.get("drop"):
        # Lines the program emits at an instant nothing about the program
        # decides. Used only where the surrounding lines are worth scoring and
        # these are not; every use names the finding that justifies it.
        text = "\n".join(line for line in text.split("\n")
                         if not any(re.search(p, line) for p in norm["drop"]))
    if norm.get("dedupe"):
        # Keep one of each matching line and every line's multiplicity
        # otherwise. Used where the *count* of a line follows from read
        # segmentation -- a 64 KiB send may arrive as three reads or four --
        # while the surrounding lines are one per connection and do count.
        seen = set()
        kept = []
        for line in text.split("\n"):
            if any(re.search(p, line) for p in norm["dedupe"]):
                if line in seen:
                    continue
                seen.add(line)
            kept.append(line)
        text = "\n".join(kept)
    if norm.get("sort") or norm.get("unique"):
        lines = text.split("\n")
        if norm.get("unique"):
            lines = _unmerge(sorted(set(lines)))
        else:
            lines = sorted(lines)
        text = "\n".join(lines)
    return text


def _finish(raw, flags, threads):
    # Decide every thread's label up front, from the lines that thread wrote
    # rather than from the order they reached the transcript. Without this a
    # start-up race between two threads renames both of them, and the item is
    # unstable against the baseline itself -- see normalize.seed_thread_map.
    threads.clear()
    threads.update(seed_thread_map(raw))

    return {key: _apply_norm(val, threads, flags.get(key))
            for key, val in raw.items()}


# --------------------------------------------------------------------------
# process control


def _listening(port):
    """Is something holding a listening TCP socket on this port?

    Asked of the kernel's table rather than by connecting. The obvious probe
    -- connect, close -- is a connection the server accepts and logs, and by
    the time it asks getpeername() for the address to log, the probe's socket
    can already be gone: getSocketPeerAddr zeroes the sockaddr when that call
    fails, so the line comes out as `onTcpConnected (0.0.0.0:0)`. Which of the
    two it writes is a race between the probe's close and the server's own
    connected callback, and it was the one nondeterministic line in the suite:
    over 40 full replays of the pinned tree, 39 matched the recording and one
    did not, on daytime.core/log. Reading /proc/net/tcp costs the server
    nothing and cannot be logged.

    State 0A is TCP_LISTEN. Field 1 is `<hex-addr>:<hex-port>`; only the port
    half is matched, so a server bound to 0.0.0.0 and one bound to 127.0.0.1
    both answer, and both v4 and v6 tables are read for the same reason.
    """
    want = "%04X" % port
    for path in ("/proc/net/tcp", "/proc/net/tcp6"):
        try:
            with open(path) as handle:
                handle.readline()                        # column headings
                for line in handle:
                    field = line.split()
                    if len(field) > 3 and field[3] == "0A" \
                            and field[1].rsplit(":", 1)[-1] == want:
                        return True
        except OSError:
            pass
    return False


def _wait_ready(ready, proc, timeout=STARTUP_TIMEOUT):
    """Block until the server is actually accepting, or give up."""
    if ready is None:
        return True
    deadline = time.time() + timeout
    kind = ready.get("kind", "tcp")
    while time.time() < deadline:
        if proc is not None and proc.poll() is not None:
            return False
        if kind == "tcp":
            if _listening(ready["port"]):
                return True
            time.sleep(0.05)
        elif kind == "udp":
            # Nothing to connect to; a bound UDP socket is only observable by
            # the absence of an ICMP refusal, which is racy. Probe by trying
            # to bind the port ourselves: while the server holds it we fail.
            probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            try:
                probe.bind((HOST, ready["port"]))
            except OSError:
                probe.close()
                return True
            probe.close()
            time.sleep(0.05)
        else:
            time.sleep(ready.get("seconds", 1.0))
            return True
    return False


def _alive(pid):
    """Is this pid a process that still exists and has not already exited?

    A daemon is nobody's child here -- the double fork is the point -- so
    waitpid cannot answer this and nothing reaps it. In a container whose PID
    1 is a shell rather than an init, an exited daemon can sit as a zombie
    indefinitely, and `os.kill(pid, 0)` says a zombie is alive. It is not; it
    has released its port and closed its files, which is everything the
    fixture is asking about. So the state field decides.
    """
    try:
        with open("/proc/%d/stat" % pid) as handle:
            stat = handle.read()
    except (OSError, IOError):
        return False
    tail = stat.rpartition(")")[2].split()
    return bool(tail) and tail[0] != "Z"


def _pids_named(name):
    """Every live process whose command name is exactly `name`.

    /proc rather than pgrep: procps is not something this image is promised to
    carry, and a missing binary would arrive here as "the daemon is not
    running" -- an answer about the fixture wearing the clothes of an answer
    about the program. comm is the kernel's truncation of the name to fifteen
    characters, so the comparison truncates too.
    """
    want = name[:15]
    found = set()
    for entry in os.listdir("/proc"):
        if not entry.isdigit():
            continue
        try:
            with open("/proc/%s/comm" % entry) as handle:
                comm = handle.read().strip()
        except (OSError, IOError):
            continue
        if comm == want and _alive(int(entry)):
            found.add(int(entry))
    return found


def _await_gone(pids, timeout):
    deadline = time.time() + timeout
    while time.time() < deadline:
        if not any(_alive(pid) for pid in pids):
            return True
        time.sleep(0.05)
    return not any(_alive(pid) for pid in pids)


def _port_free(ready, timeout):
    """Wait until nothing is listening on the session's port.

    SO_REUSEADDR deliberately: a socket in TIME_WAIT is not a server that
    failed to let go, and scoring it as one would make the item depend on how
    fast the previous connections happened to close.
    """
    if ready is None or ready.get("kind", "tcp") != "tcp":
        return True
    deadline = time.time() + timeout
    while True:
        probe = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        probe.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        try:
            probe.bind((HOST, ready["port"]))
            return True
        except OSError:
            pass
        finally:
            probe.close()
        if time.time() >= deadline:
            return False
        time.sleep(0.05)


OUTPUT_CAP = 262144
LOG_CAP = 262144


def _cut_at_line(data, cap):
    """Truncate to at most `cap` bytes, ending on a line boundary.

    Cutting mid-line makes the surviving fragment depend on exactly how many
    bytes a producer had emitted when the reader hit the cap, which is a race.
    The set of whole lines before the cap is not.
    """
    head = data[:cap]
    nl = head.rfind(b"\n")
    return head[:nl] if nl >= 0 else head


class _Drain(threading.Thread):
    """Keep a bounded prefix of a child's output and throw the rest away.

    chargen_client writes every byte it receives to stdout, and the chargen
    module logs a line per send, so an unbounded capture is a memory bomb --
    the first attempt at this reached 26 GB. Keeping a fixed prefix and a
    "there was more" marker is both bounded and reproducible: the prefix of a
    deterministic stream is deterministic, and whether the stream overran the
    cap is a stable property of the program, not of the machine's speed.
    """

    def __init__(self, stream, cap=OUTPUT_CAP):
        threading.Thread.__init__(self)
        self.daemon = True
        self.stream = stream
        self.cap = cap
        self.data = b""
        self.total = 0

    def run(self):
        while True:
            try:
                # read1, not read: read(n) on a BufferedReader blocks until it
                # has n bytes or the stream ends, so a capture that is also
                # watched while it runs sees nothing at all until 64 KiB have
                # accumulated. Which bytes end up here is unaffected -- the
                # prefix of a stream does not depend on how it was cut into
                # reads -- but when they arrive is, and one fixture waits on
                # that. Measured before the change: a wait for ten thread ids
                # in this stream ran its whole two-minute timeout out while
                # every one of them was already sitting in the pipe.
                chunk = self.stream.read1(65536)
            except (ValueError, OSError):
                break
            if not chunk:
                break
            self.total += len(chunk)
            if len(self.data) < self.cap:
                self.data += chunk[:self.cap - len(self.data)]
        try:
            self.stream.close()
        except OSError:
            pass

    def text(self):
        if self.total <= self.cap:
            return self.data.decode("latin-1")
        return _cut_at_line(self.data, self.cap).decode("latin-1") + \
            "\n<truncated: output continued past %d bytes>" % self.cap


# Everything the candidate produced is run through this, so that a program the
# candidate wrote cannot write the reward file the grader is about to create.
# Empty while recording; set by the verifier to a setpriv invocation.
LAUNCH_PREFIX = [p for p in os.environ.get("ISEFIX_LAUNCH_PREFIX", "").split()
                 if p]

# The working directory of every program under test, and the destination of
# everything they create *relative to it* -- which, in this project, is the
# driver's `./drvtmp` and nothing else.
#
# Deliberately not the bindir. The verifier drops privilege for the programs it
# runs (LAUNCH_PREFIX above), so whatever directory they write to has to be
# writable by the user that lands on, and the bindir must not be: it holds
# `isedrv`, the one binary in there the verifier owns and the candidate does
# not, and a writable bindir would let a program run early in the suite rewrite
# it before it runs.
#
# Nothing in the suite observes this path, because nothing the driver prints
# spells it absolutely.
RUNDIR = "/tmp/iserun"


def _prepare_dirs(bindir):
    """Clean, writable scratch for one session; returns the working directory.

    Two directories, because ise splits the difference: the log file's name
    comes from `getAppSubPath("log")`, which is relative to the *executable*,
    not to the working directory, so `<bindir>/log` is where the servers write
    no matter where they are started from. That directory therefore has to be
    writable by the user the programs run as -- but only that directory, not
    the bindir around it, which is what keeps `isedrv` out of reach.

    Modes rather than a chown: the recorder runs with no prefix at all, the
    verifier's setpriv may name any uid, and neither holds anything here it
    needs to keep.
    """
    shutil.rmtree(RUNDIR, ignore_errors=True)
    os.makedirs(RUNDIR)
    os.chmod(RUNDIR, 0o777)
    logdir = os.path.join(bindir, "log")
    shutil.rmtree(logdir, ignore_errors=True)
    os.makedirs(logdir)
    os.chmod(logdir, 0o777)
    return RUNDIR


def _spawn(argv, cwd, cap=OUTPUT_CAP):
    proc = subprocess.Popen(LAUNCH_PREFIX + list(argv), cwd=cwd,
                            stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, start_new_session=True)
    drain = _Drain(proc.stdout, cap)
    drain.start()
    return proc, drain


def _stop(proc, drain, grace=SHUTDOWN_GRACE):
    if proc.poll() is None:
        proc.send_signal(signal.SIGTERM)
    try:
        proc.wait(timeout=grace)
        timed_out = False
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait()
        timed_out = True
    drain.join(timeout=10.0)
    return drain.text(), timed_out


def _session_flags(session):
    """Collect the per-item transform flags a session declares."""
    flags = {}
    for step in session.get("steps", []):
        if "id" in step and step.get("norm"):
            flags[step["id"]] = step["norm"]
    if session.get("norm_stdout"):
        flags[session["id"] + "/stdout"] = session["norm_stdout"]
    # Every log line is written by an event-loop or assistor thread that is not
    # synchronised with the client's step sequence, so the order in which two
    # connections' events land in the file is never promised -- measured: a
    # four-connection daytime fixture interleaved #3's disconnect with #4's
    # connect on one run in three. Sorting is therefore the floor for every
    # session, not an opt-in; what each session may add is something stronger.
    log = dict(session.get("norm_log") or {})
    log.setdefault("sort", True)
    flags[session["id"] + "/log"] = log
    for extra in session.get("extra_items", []):
        flags[session["id"] + "/" + extra["suffix"]] = extra["norm"]
    return flags


def _read_logs(bindir, cap=LOG_CAP):
    logdir = os.path.join(bindir, "log")
    if not os.path.isdir(logdir):
        return "<no-log-dir>"
    chunks = []
    for name in sorted(os.listdir(logdir)):
        path = os.path.join(logdir, name)
        with open(path, "rb") as handle:
            body = handle.read(cap + 1)
        if len(body) > cap:
            body = _cut_at_line(body, cap) + \
                b"\n<truncated: log continued past %d bytes>" % cap
        chunks.append("--- %s ---\n%s" % (name, body.decode("latin-1")))
    return "\n".join(chunks) if chunks else "<no-log-files>"


# --------------------------------------------------------------------------
# session drivers


def _run_server_session(session, bindir, rundir):
    """Start a server, run the interactions, stop it, collect the outputs."""
    items = {}
    threads = {}
    flags = _session_flags(session)
    exe = os.path.join(bindir, session["program"])
    proc, drain = _spawn([exe] + list(session.get("args", ["--nodaemon"])), rundir)

    ready = _wait_ready(session.get("ready"), proc)
    if not ready:
        out, _ = _stop(proc, drain)
        for step in session["steps"]:
            if "id" in step:
                items[step["id"]] = "<server-never-ready>"
        items[session["id"] + "/stdout"] = "<server-never-ready>\n" + out
        items[session["id"] + "/log"] = "<server-never-ready>"
        _add_extras(items, session)
        return _finish(items, flags, threads)

    for step in session["steps"]:
        op = step["op"]
        if op == "sleep":
            time.sleep(step["seconds"])
            continue
        if op == "signal":
            proc.send_signal(getattr(signal, step["signal"]))
            continue
        if op == "await_threads":
            # Wait until the program has *shown* the threads it promises,
            # rather than until a clock runs out. Sleeping a fixed four
            # seconds and then counting ids makes the count a measurement of
            # how much CPU this box had to spare: one pool thread that has not
            # reached its first line yet turns a correct build into a failed
            # item. The count is the promise, so the count is what is waited
            # for. Giving up is recorded by simply not waiting any longer --
            # the item that follows then shows the shortfall.
            #
            # Watched in the log and not on stdout, even though stdout is what
            # the count is finally taken from. The program's stdout is a pipe,
            # so libc buffers it in 4 KiB blocks and the lines do not leave the
            # process until it exits; the log file is written through. Both
            # carry the same per-task thread id, so the log answers "have all
            # ten run yet" at the moment it becomes true, and the flush that
            # shutdown forces puts the same ids on stdout for scoring.
            deadline = time.time() + step.get("timeout", 60)
            while time.time() < deadline:
                seen = set(RE_THREAD_FIELD.findall(_read_logs(bindir)))
                if len(seen) >= step["count"]:
                    break
                time.sleep(0.1)
            continue
        if op == "spawn":
            # Start a second copy of the same program and score what it says.
            second, second_drain = _spawn(
                [exe] + list(step.get("args", ["--nodaemon"])), rundir)
            try:
                second.wait(timeout=step.get("timeout", 20))
                rc = str(second.returncode)
            except subprocess.TimeoutExpired:
                second.kill()
                second.wait()
                rc = "<timeout>"
            second_drain.join(timeout=10.0)
            items[step["id"]] = "rc=%s\n%s" % (rc, second_drain.text())
            continue
        raw = OPS[op](step)
        items[step["id"]] = raw

    stdout, killed = _stop(proc, drain)
    items[session["id"] + "/stdout"] = ("<had-to-kill>\n" if killed else "") + stdout
    items[session["id"] + "/log"] = _read_logs(bindir)
    _add_extras(items, session)
    return _finish(items, flags, threads)


def _add_extras(items, session):
    """Derive additional scored items from a session's stdout or log.

    Used where one raw output supports two independent expectations, e.g. the
    thread pool's console output is scored both for its line format and for
    how many distinct threads produced it.
    """
    for extra in session.get("extra_items", []):
        src = items.get(session["id"] + "/" + extra["from"], "")
        items[session["id"] + "/" + extra["suffix"]] = src


def _run_cli_session(session, bindir, rundir):
    """Run a program to completion and score stdout, stderr and exit code."""
    items = {}
    threads = {}
    flags = _session_flags(session)
    for step in session["steps"]:
        argv = [os.path.join(bindir, step["program"])] + list(step.get("args", []))
        try:
            proc = subprocess.run(
                LAUNCH_PREFIX + argv, cwd=rundir,
                stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                timeout=step.get("timeout", 60))
            rc, out, err = proc.returncode, proc.stdout, proc.stderr
        except subprocess.TimeoutExpired as exc:
            # A program that keeps running is itself the answer for an
            # argument that is not a recognised option.
            rc, out, err = "<did-not-exit>", exc.stdout or b"", exc.stderr or b""
        items[step["id"]] = "rc=%s\n--stdout--\n%s--stderr--\n%s" % (
            rc, out.decode("latin-1"), err.decode("latin-1"))
    return _finish(items, flags, threads)


def _terminate(proc, marker):
    """Stop a client that was never going to stop by itself."""
    proc.send_signal(signal.SIGTERM)
    try:
        proc.wait(timeout=10)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait()
    return marker


def _await_client(proc, drain, step):
    """Wait for a client to exit, or for it to have produced enough to score.

    A client that never ends on its own -- chargen's does not -- has to be
    stopped by us, and what it is stopped *on* decides whether the item is an
    expectation or a measurement of this machine. A wall-clock window is the
    latter: the same binary under load emits a shorter prefix inside the same
    four seconds, the transcript shrinks, and the item fails against a
    baseline it matches perfectly. `until_bytes` is a property of the stream.

    The recorded text is the drain's first `cap` bytes, so `until_bytes` must
    be strictly larger than `cap`: stopping at exactly the cap would leave
    "was there more?" -- and with it the truncation marker, and with it the
    length of the kept prefix -- decided by how much of the last read had
    arrived. Overshooting the cap makes both answers constant. The timeout
    beside it is a backstop against a client that has stopped producing
    altogether, and reaching it is recorded rather than smoothed over.
    """
    timeout = step.get("timeout", 30)
    limit = step.get("until_bytes")
    if limit is None:
        try:
            proc.wait(timeout=timeout)
            return str(proc.returncode)
        except subprocess.TimeoutExpired:
            return _terminate(proc, "<timeout-sigterm>")
    if limit <= drain.cap:
        raise SessionError("until_bytes %d must exceed cap %d for %s"
                           % (limit, drain.cap, step["id"]))
    deadline = time.time() + timeout
    while time.time() < deadline:
        if proc.poll() is not None:
            return str(proc.returncode)
        if drain.total >= limit:
            return _terminate(proc, "<stopped-at-cap>")
        time.sleep(0.05)
    return _terminate(proc, "<never-reached-cap>")


def _run_pair_session(session, bindir, rundir):
    """Start a server, then run a client example against it to completion."""
    items = {}
    threads = {}
    flags = _session_flags(session)
    server, drain = _spawn(
        [os.path.join(bindir, session["program"])] +
        list(session.get("args", ["--nodaemon"])), rundir)
    if not _wait_ready(session.get("ready"), server):
        _stop(server, drain)
        raise SessionError("server %s never became ready" % session["program"])

    for step in session["steps"]:
        if step["op"] == "sleep":
            time.sleep(step["seconds"])
            continue
        if step["op"] == "client":
            proc, client_drain = _spawn(
                [os.path.join(bindir, step["program"])] + list(step.get("args", [])),
                rundir, step.get("cap", OUTPUT_CAP))
            rc = _await_client(proc, client_drain, step)
            client_drain.join(timeout=10.0)
            items[step["id"]] = "rc=%s\n%s" % (rc, client_drain.text())
            continue
        items[step["id"]] = OPS[step["op"]](step)

    stdout, killed = _stop(server, drain)
    items[session["id"] + "/stdout"] = ("<had-to-kill>\n" if killed else "") + stdout
    items[session["id"] + "/log"] = _read_logs(bindir)
    _add_extras(items, session)
    return _finish(items, flags, threads)


# Set by sanitize.py to a callable(tag, text). Left None everywhere else, so
# recording behaves exactly as it did before this existed. It exists because
# the driver session is the one place raw output is read and then dropped --
# stderr is kept only to quote in a failure message -- and a sanitizer writes
# its diagnostics there. A screen that cannot see them calls the session clean.
STDERR_SINK = None


def _note_stderr(tag, text):
    if STDERR_SINK is not None and text:
        STDERR_SINK(tag, text)


def split_driver_blocks(text):
    """[(name, unscored_reason_or_None, body)] from a driver transcript.

    The driver prints `### <name>` before every case and, for a case whose
    output is not a function of its inputs, `### unscored: <reason>` directly
    under it. Reading the marker out of the transcript rather than keeping the
    list here means the reason lives once, next to the code that earned it.
    """
    blocks = []
    name, reason, body = None, None, []
    for line in text.splitlines(True):
        if line.startswith("### "):
            if line.startswith("### unscored:"):
                if name is None or body:
                    raise SessionError("misplaced unscored marker: %r" % line)
                reason = line[len("### unscored:"):].strip()
                continue
            if name is not None:
                blocks.append((name, reason, "".join(body)))
            name, reason, body = line[4:].rstrip("\n"), None, []
            continue
        if name is None:
            raise SessionError("driver output before any case header: %r" % line)
        body.append(line)
    if name is not None:
        blocks.append((name, reason, "".join(body)))
    return blocks


def _run_driver_session(session, bindir, rundir):
    """Run the characterisation driver and score one item per case block.

    The driver is a whole session's worth of scored items in one process, so
    the two ways it can go wrong quietly both have to be refused outright: a
    case that crashed the process takes every later case out of the
    denominator, and a case renamed in C++ but not here would silently stop
    being scored. Both show up as a set difference, so both are checked as one.
    """
    argv = [os.path.join(bindir, session["program"])] + list(session.get("args", []))
    try:
        proc = subprocess.run(LAUNCH_PREFIX + argv, cwd=rundir,
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                              timeout=session.get("timeout", 300))
    except subprocess.TimeoutExpired:
        raise SessionError("driver did not finish within %ds"
                           % session.get("timeout", 300))
    _note_stderr(session["id"], proc.stderr.decode("latin-1"))
    if proc.returncode != 0:
        raise SessionError("driver exited %s: %s" % (
            proc.returncode, proc.stderr.decode("latin-1")[-400:]))

    blocks = split_driver_blocks(proc.stdout.decode("latin-1"))

    # The driver's own count of the cases it ran, against the blocks seen.
    tail = [name for name, _r, _b in blocks if name.startswith("done ")]
    if len(tail) != 1 or tail[0] != "done %d cases" % (len(blocks) - 1):
        raise SessionError("driver ran %d blocks but reported %r"
                           % (len(blocks) - 1, tail))

    got = set(name for name, reason, _b in blocks
              if reason is None and not name.startswith("done "))
    want = set(step["case"] for step in session["steps"])
    if got != want:
        raise SessionError(
            "driver case set differs from the catalogue: only in driver %s; "
            "only in catalogue %s" % (sorted(got - want), sorted(want - got)))

    bodies = dict((name, body) for name, _r, body in blocks)
    items = dict((step["id"], bodies[step["case"]]) for step in session["steps"])
    return _finish(items, _session_flags(session), {})


def _stop_daemon(pids, ready):
    """Stop what is running, and say how much of it there was.

    The count is in the recorded text on purpose. "Everything I signalled
    stopped, and the port came back" is true of a machine where nothing was
    ever running, so without the count this item is satisfied by a tree that
    builds no programs at all -- measured: on the untouched baseline, which
    does not configure, let alone compile, this was the one item in the whole
    suite that passed. Naming the number makes absence produce a different
    string rather than the same one.
    """
    for pid in pids:
        try:
            os.kill(pid, signal.SIGTERM)
        except OSError:
            pass
    stopped = _await_gone(pids, SHUTDOWN_GRACE + 15)
    return "signalled=%d stopped-on-sigterm=%s port-released=%s" % (
        len(pids), "yes" if stopped else "no",
        "yes" if _port_free(ready, 10.0) else "no")


def _run_daemon_session(session, bindir, rundir):
    """Start a program the way it starts when nobody tells it otherwise.

    Every other server fixture passes --nodaemon, because a foreground child
    is what the rest of this harness knows how to wait on and to signal. That
    leaves the default path -- fork, setsid, fork again, let the launcher exit
    -- asserted by nothing, and it is a path that can be lost without any
    other fixture noticing: a program that simply never detaches still answers
    every connection those fixtures make. What this one scores is the
    difference. The launcher exits, and something that is not the launcher is
    serving; then it stops on SIGTERM and gives the port back.

    There is no child left to signal by the time it matters, so the daemon is
    found by name among the processes that were not there before it started.
    """
    items = {}
    threads = {}
    flags = _session_flags(session)
    program = session["program"]
    before = _pids_named(program)
    proc, drain = _spawn([os.path.join(bindir, program)] +
                         list(session.get("args", [])), rundir)
    try:
        proc.wait(timeout=session.get("detach_timeout", 30))
        launcher = str(proc.returncode)
    except subprocess.TimeoutExpired:
        # Deliberately not killed here: a launcher still running is one of the
        # answers this session exists to record, and the pid scan below has to
        # see the process to report it.
        launcher = "<never-exited>"

    ready = _wait_ready(session.get("ready"), None)
    live = sorted(_pids_named(program) - before)

    for step in session["steps"]:
        op = step["op"]
        if op == "detached":
            items[step["id"]] = (
                "launcher-rc=%s live-processes=%d launcher-still-alive=%s "
                "serving=%s" % (launcher, len(live),
                                "yes" if proc.pid in live else "no",
                                "yes" if ready else "no"))
        elif not ready:
            # Ahead of stop_daemon, not behind it. Nothing that follows a
            # daemon that never came up is a measurement of the daemon.
            items[step["id"]] = "<daemon-never-ready>"
        elif op == "stop_daemon":
            items[step["id"]] = _stop_daemon(live, session.get("ready"))
        else:
            items[step["id"]] = OPS[op](step)

    for pid in live:
        if _alive(pid):
            try:
                os.kill(pid, signal.SIGKILL)
            except OSError:
                pass
    _await_gone(live, 10.0)
    drain.join(timeout=10.0)
    items[session["id"] + "/stdout"] = drain.text()
    items[session["id"] + "/log"] = _read_logs(bindir)
    _add_extras(items, session)
    return _finish(items, flags, threads)


RUNNERS ={"server": _run_server_session, "cli": _run_cli_session,
          "pair": _run_pair_session, "driver": _run_driver_session,
          "daemon": _run_daemon_session}


def run_session(session, bindir):
    rundir = _prepare_dirs(bindir)
    return RUNNERS[session.get("kind", "server")](session, bindir, rundir)


def expected_item_ids(session):
    """Every item id a session is expected to produce, without running it."""
    ids = [step["id"] for step in session["steps"] if "id" in step]
    if session.get("kind", "server") in ("server", "pair", "daemon"):
        ids += [session["id"] + "/stdout", session["id"] + "/log"]
    ids += [session["id"] + "/" + extra["suffix"] for extra in session.get("extra_items", [])]
    return ids
