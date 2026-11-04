"""The behaviour fixture catalogue.

Every scored input the task ever uses is declared here, once. Each one names
the stratum it belongs to and whether it is adversarial, so the balance the
suite claims can be counted rather than asserted.

Strata
  core       the documented happy path of a protocol
  text       character-set and encoding edges
  numeric    field values at 0, 1, the limit, one past the limit, and wrapped
  width      payload and buffer-boundary sizes
  malformed  input the protocol says nothing about
  state      several exchanges whose result depends on the order they ran in
  lifecycle  startup, refusal to start, and shutdown

Reads are always bounded by a declared stop condition. Where a protocol keeps
the connection open, the fixture asks the server to close it ("quit"), closes
its own side, or reads an exact byte count -- never "read until quiet".
"""
import base64
import struct

from .normalize import EXE_NAMES

# --------------------------------------------------------------------------
# helpers

GREET = b"Welcome to the simple echo server, type 'quit' to exit.\r\n"
QUIT = b"quit\n"

ECHO_PORT, DISCARD_PORT, DAYTIME_PORT, CHARGEN_PORT = 10000, 10001, 10002, 10003
HTTP_PORT, HELLO_PORT, UDP_PORT = 8080, 12345, 8000

# The fifteen executables the project's examples come to -- one per
# subdirectory of examples/, which is one more than the legacy
# examples/CMakeLists.txt ever listed. One list, shared with the normaliser,
# which needs the same names to erase the directory they were built into.
EXES = list(EXE_NAMES)


def b64(data):
    return base64.b64encode(data).decode("ascii")


def until_close(timeout=10.0):
    return {"until": {"close": 1}, "timeout": timeout}


def tcp(ident, stratum, adv, port, send, until=None, timeout=10.0, http=False,
        shutdown_write=False, norm=None):
    """One TCP connection, one send, one bounded read."""
    exchange = {"send_b64": b64(send), "timeout": timeout}
    exchange["until"] = until if until is not None else {"close": 1}
    if http:
        exchange["http"] = True
    if shutdown_write:
        exchange["shutdown_write"] = True
        if send:
            exchange["hold"] = SEND_HOLD
    step = {"id": ident, "stratum": stratum, "adv": adv, "op": "tcp",
            "port": port, "exchanges": [exchange]}
    if norm:
        step["norm"] = norm
    return step


def echo(name, payload, stratum="core", adv=False, port=ECHO_PORT, prefix="echo"):
    """An echo-protocol case: send the payload, then ask the server to close.

    Appending "quit" makes the transcript self-terminating, so the read stops
    on the server's own close rather than on a timer.
    """
    return tcp("%s/%s" % (prefix, name), stratum, adv, port, payload + QUIT)


def sink(name, payload, port, stratum="core", adv=False, prefix="discard"):
    """A protocol that never replies: send, close our side, read to EOF."""
    return tcp("%s/%s" % (prefix, name), stratum, adv, port, payload,
               shutdown_write=True)


# How long a connection that sends nothing is held open before it is closed.
SETTLE = 0.25

# How long a payload is left with the server before the FIN that follows it.
#
# A step that sends and shuts the write side down in the same breath puts the
# data and the FIN in front of the event loop together, and ise's discard
# module only logs `[<name>] Discarded N bytes.` from onTcpRecvComplete: which
# of the two the loop picks up first decides whether the bytes are ever read or
# are thrown away with the connection. Neither outcome is a property of the
# program, so neither can be an expectation -- and this one had already been
# recorded as if it were, and had already gone off. check_cmake_trace.py's
# CALLVAR fixture, which changes no source file, came back 431 of 432 with
# discard.core/log missing and one extra id in the sorted Discarded block: the
# other side of the race, on a loaded box.
#
# Measured by authoring/measure_sink_hold.py against a built tree, 40 replays
# of each session at each hold, while the box was building fixtures:
#
#   hold=0     discard.core  5 Discarded lines (ids 3,5,6,7,8)  in 40 of 40
#              4in1.core     0 Discarded lines                  in 40 of 40
#   hold=0.25  discard.core  8 Discarded lines (ids 1..8)       in 40 of 40
#              4in1.core    26 Discarded lines (ids 1,5,7)      in 40 of 40
#
# So the pause does not paper over the race, it settles it: every connection
# that sends is read, every time, and the expectation gains the lines the race
# was eating -- three more connections in discard.core, and in 4in1.core the
# discard *module*'s one-line-per-byte splitter, 10 + 11 + 5 lines for the
# three payloads, which nothing scored before.
SEND_HOLD = 0.25

# The udp sessions need the same treatment at the other end of their life.
#
# ise writes "Killed N udp worker thread." when a worker has not left its loop
# MAX_THREAD_WAIT_FOR_SECS after being asked to. A worker parked in
# UdpRequestQueue::extractPacket's `while (packetCount_ == 0) condition_.wait();`
# can never leave -- the predicate has no terminate check -- so that line is the
# ordinary outcome and its *absence* is the rare one: the terminate flag landing
# while the worker was still dispatching the session's last packet.
#
# Measured by /work/src/killrace/arms.py: two arms interleaved run by run so
# both saw the same machine, 80 runs of each of the five udp sessions per arm
# (400 session-runs each), pinned to one CPU because contention is what widens
# the window.
#
#   hold=0     udp.core 3/80, udp.malformed 2/80, udp.numeric 2/80,
#              udp.width 2/80, pair.udp_client 0/80 with no Killed line
#              -- 9 of 400 session-runs shut down clean
#   hold=0.5   0 of 400. Every session, every run, "Killed 1 udp worker thread."
#
# So the pause settles the race rather than papering over it: the worker is back
# in its steady state before the server is taken away, and the line the
# expectation records is the one that is then always produced. The decision
# between this and dropping the line from the log was fixed before the run
# (NOTES 7.16); this is the arm it chose.
SHUTDOWN_HOLD = 0.5


def shutdown_settled(steps):
    """`steps` with the trailing pause that parks the udp worker first."""
    return list(steps) + [{"op": "sleep", "seconds": SHUTDOWN_HOLD}]


def settle(step):
    """Hold the connection open briefly before closing it.

    A client that connects and closes in the same breath races the server's
    own accept-side callback. ise reads the peer address with getpeername()
    the first time it logs a connection, and getSocketPeerAddr zeroes the
    sockaddr when that call fails, which it does for a socket whose peer has
    already gone: the line comes out as `onTcpConnected (0.0.0.0:0)`, or, if
    the close wins by more, not at all. Both were measured on the pinned tree,
    one step per session, with the readiness probe out of the picture: 15
    isolated replays of echo/open_close split 8 zeroed to 7 absent, and all 15
    of discard/empty logged the zeroed address.

    Neither outcome is a property of the program, so neither can be an
    expectation. A quarter of a second removes the race without removing the
    case -- the step still sends nothing, and what the server logs for it is
    still the server's decision. 40 isolated replays of each, after this: one
    real peer address, every time.
    """
    return dict(step, settle=SETTLE)


def http_case(name, raw, stratum="core", adv=False, prefix="http",
              port=HTTP_PORT, norm=None):
    # Every response this server sends, it sends at once; the only reads that
    # run out the clock are the ones where it is waiting for a request body
    # that will never come, and that wait is itself the recorded answer.
    return tcp("%s/%s" % (prefix, name), stratum, adv, port, raw, http=True,
               timeout=4.0, norm=norm)


def get(path, version=b"1.1", host=True):
    head = b"GET " + path + b" HTTP/" + version + b"\r\n"
    if host:
        head += b"Host: 127.0.0.1\r\n"
    return head + b"\r\n"


def udp_packet(action, seq, size_field=None, body=b""):
    """A UdpPacketHeader followed by a length-prefixed string.

    size_field is written verbatim so a fixture can declare a length that does
    not match the bytes that follow it.
    """
    if size_field is None:
        size_field = len(body)
    return struct.pack("<II", action & 0xFFFFFFFF, seq & 0xFFFFFFFF) + \
        struct.pack("<I", size_field & 0xFFFFFFFF) + body


def udp(ident, stratum, adv, payload, expect=1, timeout=1.5):
    return {"id": ident, "stratum": stratum, "adv": adv, "op": "udp",
            "port": UDP_PORT, "send_b64": b64(payload),
            "expect_datagrams": expect, "timeout": timeout}


AC_HELLO, AC_ACK = 100, 200
EIGHT_MB = 8 * 1024 * 1024


# --------------------------------------------------------------------------
# sessions


def _echo_core():
    return {
        "id": "echo.core", "program": "echo_server",
        "ready": {"kind": "tcp", "port": ECHO_PORT},
        "steps": [
            # Connect and say nothing: the greeting alone is the whole answer.
            tcp("echo/greeting", "core", False, ECHO_PORT, b"",
                until={"bytes": len(GREET)}),
            echo("hello", b"hello\n"),
            echo("empty_line", b"\n"),
            echo("crlf", b"abc\r\n"),
            echo("spaces", b"   padded   \n"),
            echo("two_lines", b"first\nsecond\n"),
            echo("five_lines", b"a\nb\nc\nd\ne\n"),
            # trimString() runs before the "quit" comparison, so these probe
            # whether the command is matched on the trimmed or the raw line.
            echo("quit_padded", b" quit \nunreachable\n", "state", True),
            echo("quit_upper", b"QUIT\n", "state", True),
            echo("quit_substring", b"quitter\n", "state", True),
            echo("quit_tab", b"\tquit\t\n", "state", True),
        ],
    }


def _echo_text():
    return {
        "id": "echo.text", "program": "echo_server",
        "ready": {"kind": "tcp", "port": ECHO_PORT},
        "steps": [
            echo("utf8_latin", "héllo wörld\n".encode("utf-8"), "text", True),
            echo("utf8_cjk", "你好，世界\n".encode("utf-8"), "text", True),
            echo("utf8_emoji", "x\U0001F600y\n".encode("utf-8"), "text", True),
            echo("gbk_bytes", b"\xc4\xe3\xba\xc3\n", "text", True),
            echo("latin1_high", b"\xe9\xfc\xff\n", "text", True),
            echo("nul_embedded", b"a\x00b\n", "text", True),
            echo("all_control", bytes(range(1, 10)) + b"\n", "text", True),
            echo("del_and_high", b"\x7f\x80\x81\n", "text", True),
            echo("utf8_bom", b"\xef\xbb\xbfhi\n", "text", True),
            echo("lone_cr", b"a\rb\n", "text", True),
            echo("only_cr_lf", b"\r\n", "text", True),
            echo("backslashes", b"a\\nb\\tc\n", "text", True),
            echo("percent_fmt", b"%s %d %n %%\n", "text", True),
        ],
    }


def _echo_width():
    steps = []
    # Around the 1 KiB packet-buffer growth step and the 4 KiB read chunk.
    for size in (1, 2, 1023, 1024, 1025, 4095, 4096, 4097, 8191, 8192, 65535):
        steps.append(echo("len_%d" % size, b"x" * size + b"\n", "width", True))
    # One payload far past any internal buffer, sent as a single write.
    steps.append(echo("len_262144", b"y" * 262144 + b"\n", "width", True))
    return {
        "id": "echo.width", "program": "echo_server",
        "ready": {"kind": "tcp", "port": ECHO_PORT}, "steps": steps,
    }


def _echo_state():
    return {
        "id": "echo.state", "program": "echo_server",
        "ready": {"kind": "tcp", "port": ECHO_PORT},
        "steps": [
            # Open and close without sending: does the greeting still happen?
            settle({"id": "echo/open_close", "stratum": "state", "adv": True,
                    "op": "tcp", "port": ECHO_PORT, "exchanges": []}),
            # A line with no terminator, then EOF: the splitter never yields a
            # packet, so the bytes should be dropped rather than echoed.
            tcp("echo/unterminated", "state", True, ECHO_PORT, b"no newline",
                shutdown_write=True),
            # Send one byte at a time across two writes with the split inside
            # the line: reassembly is the server's problem, not the client's.
            {"id": "echo/split_write", "stratum": "state", "adv": True,
             "op": "tcp", "port": ECHO_PORT, "exchanges": [
                 {"send_b64": b64(b"part"), "until": {"bytes": len(GREET)},
                  "timeout": 10.0},
                 {"send_b64": b64(b"ial\n" + QUIT), "until": {"close": 1},
                  "timeout": 10.0}]},
            # Eight connections at once. Scored as a multiset: which worker
            # thread finishes first is not a property of the program.
            {"id": "echo/burst8", "stratum": "state", "adv": True,
             "op": "burst", "port": ECHO_PORT, "count": 8,
             "send_b64": b64(b"burst\n" + QUIT), "until": {"close": 1},
             "timeout": 15.0},
            # The connection counter in the log is sequential, so a session
            # that opens connections in a fixed order pins it.
            echo("after_burst", b"still here\n", "state", True),
        ],
        # Eight concurrent connections make the log's line order a race.
    }


def _echo_lifecycle():
    return {
        "id": "echo.lifecycle", "program": "echo_server",
        "ready": {"kind": "tcp", "port": ECHO_PORT},
        "steps": [
            echo("lifecycle_first", b"before\n", "lifecycle", False),
            # A second copy of the same executable must refuse: the single
            # instance guard flocks the program file itself.
            {"id": "echo/second_instance", "stratum": "lifecycle", "adv": True,
             "op": "spawn", "args": ["--nodaemon"], "timeout": 25},
            # ... and the first one must be unharmed by the attempt.
            echo("lifecycle_after", b"after\n", "lifecycle", True),
        ],
    }


def _maxconn_echo():
    """The capped echo server below its cap: an ordinary echo server.

    examples/max_connection is the one example the legacy
    examples/CMakeLists.txt never built. It speaks the same protocol as
    examples/echo_server and differs in exactly one place -- it refuses a
    fourth simultaneous connection -- so scoring it below the cap is what
    makes the refusal above the cap mean something rather than being the only
    thing the program is ever seen to do.
    """
    return {
        "id": "maxconn.echo", "program": "max_connection",
        "ready": {"kind": "tcp", "port": ECHO_PORT},
        "steps": [
            tcp("maxconn/greeting", "core", False, ECHO_PORT, b"",
                until={"bytes": len(GREET)}),
            echo("hello", b"hello\n", prefix="maxconn"),
            echo("two_lines", b"first\nsecond\n", prefix="maxconn"),
            echo("quit_padded", b" quit \nunreachable\n", "state", True,
                 prefix="maxconn"),
            echo("high_bytes", bytes(bytearray(range(128, 256))) + b"\n",
                 "text", True, prefix="maxconn"),
        ],
    }


def _maxconn_cap():
    """Three connections are served; the fourth and fifth are not.

    Opened one at a time, each read to its stop condition before the next is
    opened and none of them closed until the last has been answered. That is
    the fixture, and the order is the assertion: greeting, greeting, greeting,
    close, close. A server that admits a fourth simultaneous connection, or
    refuses a second, disagrees with this in a place a reader can point at.

    Opening all five at once measures something else. ise accepts every
    pending connection before it runs a single one of their callbacks, so five
    simultaneous sockets are five live connections by the time the first
    callback asks how many there are, and all five are refused -- recorded
    here, before the change, as five closes and no greeting, with one callback
    reading a count of 5 and then 6 within itself. How many a burst gets in is
    a property of the box's scheduling, not of the program, and a fixture must
    not be built on it.

    The pause before the first rung is not a window anything is measured in.
    The probe that established the port was listening has been closed by then
    but need not have been accounted for yet, and a connection count that
    still carries it would shift every rung by one. The pause after the last
    rung serves the same purpose in the other direction: five closes have to
    be accounted for before a sixth connection asks what the count is. If
    either is ever too short the items fail together and say so, which is the
    failure mode to prefer over a quiet renumbering.
    """
    return {
        "id": "maxconn.cap", "program": "max_connection",
        "ready": {"kind": "tcp", "port": ECHO_PORT},
        "steps": [
            {"op": "sleep", "seconds": 1.0},
            {"id": "maxconn/over_cap", "stratum": "state", "adv": True,
             "op": "ladder", "port": ECHO_PORT, "count": 5,
             "until": {"bytes": len(GREET)}, "timeout": 20.0},
            {"op": "sleep", "seconds": 2.0},
            # Having refused two, it must not have stopped serving.
            echo("after_cap", b"recovered\n", "state", True, prefix="maxconn"),
        ],
        # The counts in this log are the behaviour -- five connects, two
        # refusals, six disconnects -- so nothing here may collapse repeats.
        # The connection count each line reports is masked globally: the
        # numbers a ladder produces are stable, but the same log records the
        # readiness probe and its close, whose accounting is not ordered
        # against the rungs. Which side of the cap each connection fell on
        # survives in the refusal lines and, in order, in the transcript.
    }


def _discard():
    return {
        "id": "discard.core", "program": "discard_server",
        "ready": {"kind": "tcp", "port": DISCARD_PORT},
        "steps": [
            settle(sink("empty", b"", DISCARD_PORT)),
            sink("line", b"discard me\n", DISCARD_PORT),
            sink("binary", bytes(range(256)), DISCARD_PORT, "text", True),
            sink("large", b"z" * 200000, DISCARD_PORT, "width", True),
            sink("no_newline", b"partial", DISCARD_PORT, "malformed", True),
            # discard never replies and never closes first, so each of these
            # is scored on "nothing arrived and the connection stayed up".
            {"id": "discard/burst4", "stratum": "state", "adv": True,
             "op": "burst", "port": DISCARD_PORT, "count": 4,
             "send_b64": b64(b"x"), "until": {"close": 1}, "timeout": 2.0},
        ],
        # The per-read byte count is normalised globally; how *many* reads a
        # 64 KiB send takes is the same kernel decision, so those lines are
        # deduplicated too. Every other line -- one per connection, one per
        # lifecycle event -- keeps its multiplicity and is scored.
        "norm_log": {"dedupe": [r"Discarded N bytes"]},
    }


def _daytime():
    return {
        "id": "daytime.core", "program": "daytime_server",
        "ready": {"kind": "tcp", "port": DAYTIME_PORT},
        "steps": [
            tcp("daytime/plain", "core", False, DAYTIME_PORT, b""),
            # The server writes and closes; anything the client sends first
            # should not change that.
            tcp("daytime/with_junk", "malformed", True, DAYTIME_PORT, b"junk\n"),
            tcp("daytime/big_junk", "width", True, DAYTIME_PORT, b"j" * 70000),
            tcp("daytime/second", "state", True, DAYTIME_PORT, b""),
        ],
    }


def _chargen():
    steps = []
    for size in (1, 72, 73, 4096, 20000):
        steps.append(tcp("chargen/read_%d" % size,
                         "width" if size != 20000 else "core",
                         size != 20000, CHARGEN_PORT, b"",
                         until={"bytes": size}, timeout=20.0))
    return {
        "id": "chargen.core", "program": "chargen_server",
        "ready": {"kind": "tcp", "port": CHARGEN_PORT}, "steps": steps,
    }


def _fourinone():
    """All four protocols in one process, which is the point of the example."""
    return {
        "id": "4in1.core", "program": "4in1_server",
        "ready": {"kind": "tcp", "port": ECHO_PORT},
        "steps": [
            echo("4in1_hello", b"hello\n", prefix="4in1"),
            sink("4in1_discard", b"drop this\n", DISCARD_PORT, prefix="4in1"),
            tcp("4in1/daytime", "core", False, DAYTIME_PORT, b""),
            tcp("4in1/chargen", "core", False, CHARGEN_PORT, b"",
                until={"bytes": 8192}, timeout=20.0),
            # Interleave the four: each module keeps its own connection state.
            echo("4in1_echo2", b"second\n", "state", True, prefix="4in1"),
            sink("4in1_discard2", b"drop again\n", DISCARD_PORT, "state", True,
                 prefix="4in1"),
            tcp("4in1/daytime2", "state", True, DAYTIME_PORT, b""),
            # A protocol talking to the wrong module's port.
            tcp("4in1/echo_to_discard", "malformed", True, DISCARD_PORT,
                b"quit\n", shutdown_write=True),
            tcp("4in1/http_to_echo", "malformed", True, ECHO_PORT,
                get(b"/") + QUIT),
        ],
    }


def _hello():
    return {
        "id": "hello.core", "program": "hello_world",
        "ready": {"kind": "tcp", "port": HELLO_PORT},
        "steps": [
            tcp("hello/connect", "core", False, HELLO_PORT, b""),
            tcp("hello/with_data", "malformed", True, HELLO_PORT, b"ping\n"),
            tcp("hello/second", "state", True, HELLO_PORT, b""),
            {"id": "hello/burst6", "stratum": "state", "adv": True,
             "op": "burst", "port": HELLO_PORT, "count": 6,
             "until": {"close": 1}, "timeout": 15.0},
            # The single-instance guard lives in the framework, not in any one
            # example, so one example proving it leaves the rest of them
            # asserting nothing about it. Three programs of three different
            # shapes -- a tcp echo, this, and the udp server -- is enough to
            # tell "the guard still works" from "echo_server still works".
            {"id": "hello/second_instance", "stratum": "lifecycle", "adv": True,
             "op": "spawn", "args": ["--nodaemon"], "timeout": 25},
            tcp("hello/after_refusal", "lifecycle", True, HELLO_PORT, b""),
        ],
    }


def _hello_daemon():
    """The same program, started the way it starts when nobody says otherwise.

    Every other server fixture passes --nodaemon, so the default path --
    fork, setsid, fork again, and let the launcher exit -- is asserted
    nowhere, and it can be lost without any of them noticing: a program that
    quietly stops detaching still answers every connection they make. What is
    scored here is the difference between the two. The launcher exits, and
    something that is not the launcher is serving; it then stops on SIGTERM
    and gives the port back, which is the other half a daemon owes anyone who
    started it.
    """
    return {
        "id": "hello.daemon", "kind": "daemon", "program": "hello_world",
        "args": [],
        "ready": {"kind": "tcp", "port": HELLO_PORT},
        "steps": [
            {"id": "hello/daemonised", "stratum": "lifecycle", "adv": False,
             "op": "detached"},
            tcp("hello/daemon_serves", "lifecycle", False, HELLO_PORT, b""),
            {"id": "hello/daemon_stop", "stratum": "lifecycle", "adv": True,
             "op": "stop_daemon"},
        ],
    }


def _http_core():
    return {
        "id": "http.core", "program": "http_server",
        "ready": {"kind": "tcp", "port": HTTP_PORT},
        "steps": [
            http_case("root", get(b"/")),
            http_case("path", get(b"/some/path")),
            http_case("query", get(b"/p?a=1&b=2")),
            http_case("http10", get(b"/", b"1.0", host=False)),
            http_case("head", b"HEAD / HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n"),
            http_case("post", b"POST /x HTTP/1.1\r\nHost: 127.0.0.1\r\n"
                              b"Content-Length: 3\r\n\r\nabc"),
            http_case("no_host_11", b"GET / HTTP/1.1\r\n\r\n", "malformed", True),
            http_case("extra_headers", b"GET / HTTP/1.1\r\nHost: 127.0.0.1\r\n"
                                       + b"X-Pad: v\r\n" * 20 + b"\r\n"),
        ],
    }


def _http_malformed():
    cases = [
        ("bare_crlf", b"\r\n\r\n"),
        ("lf_only", b"GET / HTTP/1.1\n\n"),
        ("no_version", b"GET /\r\n\r\n"),
        ("no_path", b"GET HTTP/1.1\r\n\r\n"),
        ("bogus_verb", b"FROBNICATE / HTTP/1.1\r\n\r\n"),
        ("lowercase_verb", b"get / HTTP/1.1\r\n\r\n"),
        ("version_99", b"GET / HTTP/9.9\r\n\r\n"),
        ("version_junk", b"GET / HTTP/x.y\r\n\r\n"),
        ("only_verb", b"GET\r\n\r\n"),
        ("nul_in_path", b"GET /a\x00b HTTP/1.1\r\n\r\n"),
        ("high_bytes_path", b"GET /\xff\xfe HTTP/1.1\r\n\r\n"),
        ("header_no_colon", b"GET / HTTP/1.1\r\nBrokenHeader\r\n\r\n"),
        ("header_empty_name", b"GET / HTTP/1.1\r\n: v\r\n\r\n"),
        ("dup_content_length", b"POST / HTTP/1.1\r\nContent-Length: 1\r\n"
                               b"Content-Length: 9\r\n\r\na"),
        ("negative_length", b"POST / HTTP/1.1\r\nContent-Length: -5\r\n\r\n"),
        ("huge_length", b"POST / HTTP/1.1\r\nContent-Length: 4294967296\r\n\r\n"),
        ("space_before", b" GET / HTTP/1.1\r\n\r\n"),
        ("tabs", b"GET\t/\tHTTP/1.1\r\n\r\n"),
    ]
    return {
        "id": "http.malformed", "program": "http_server",
        "ready": {"kind": "tcp", "port": HTTP_PORT},
        "steps": [http_case(name, raw, "malformed", True, prefix="httpm")
                  for name, raw in cases],
    }


def _http_width():
    steps = []
    for size in (1, 255, 256, 1024, 4096, 8190, 16384):
        steps.append(http_case("path_%d" % size, get(b"/" + b"p" * size),
                               "width", True, prefix="httpw"))
    steps.append(http_case("many_headers", b"GET / HTTP/1.1\r\n"
                           + b"X-H: v\r\n" * 200 + b"\r\n", "width", True,
                           prefix="httpw"))
    steps.append(http_case("long_header_value",
                           b"GET / HTTP/1.1\r\nX-L: " + b"v" * 9000 + b"\r\n\r\n",
                           "width", True, prefix="httpw"))
    return {
        "id": "http.width", "program": "http_server",
        "ready": {"kind": "tcp", "port": HTTP_PORT}, "steps": steps,
    }


def _http_lifecycle():
    """The single-instance guard on a program that answers a protocol.

    Requirement 15 is about every program, and the guard is the framework's:
    it flocks the executable, not a port. Three shapes carry it already -- a
    tcp echo, a program with no server at all, and a udp server -- and this is
    the fourth, a server whose surface is a request and a response, so "the
    guard still works" cannot be read off "the one-line servers still work".
    The exchange after the refusal is the half that matters: the copy that was
    already serving has to be untouched by the attempt.
    """
    return {
        "id": "http.lifecycle", "program": "http_server",
        "ready": {"kind": "tcp", "port": HTTP_PORT},
        "steps": [
            http_case("lifecycle_first", get(b"/"), "lifecycle", False),
            {"id": "http/second_instance", "stratum": "lifecycle", "adv": True,
             "op": "spawn", "args": ["--nodaemon"], "timeout": 25},
            http_case("after_refusal", get(b"/"), "lifecycle", True),
        ],
    }


def _http_daemon():
    """Default daemonisation, on a program with a protocol to keep serving.

    hello.daemon scores the fork-setsid-fork path itself. This scores it where
    readiness is a real exchange rather than an accepted connection: the
    launcher exits, something that is not the launcher answers a GET, and
    SIGTERM to that something gives the port back. A migration that keeps the
    detach but loses the request loop passes there and fails here.
    """
    return {
        "id": "http.daemon", "kind": "daemon", "program": "http_server",
        "args": [],
        "ready": {"kind": "tcp", "port": HTTP_PORT},
        "steps": [
            {"id": "http/daemonised", "stratum": "lifecycle", "adv": False,
             "op": "detached"},
            http_case("daemon_serves", get(b"/"), "lifecycle", False),
            {"id": "http/daemon_stop", "stratum": "lifecycle", "adv": True,
             "op": "stop_daemon"},
        ],
    }


def _inspector():
    paths = [
        ("help", b"/"),
        ("info1", b"/test/info1"),
        ("info2", b"/test/info2"),
        ("info3", b"/test/info3"),
        ("info3_args", b"/test/info3?arg1=1&arg2=2"),
        ("info3_empty_args", b"/test/info3?arg1=&arg2="),
        ("info3_encoded", b"/test/info3?arg1=%20%2B&arg2=x%00y"),
        ("unknown_cmd", b"/test/nope"),
        ("unknown_cat", b"/nope/nope"),
        ("category_only", b"/test"),
        ("trailing_slash", b"/test/"),
        ("double_slash", b"//"),
        ("deep", b"/test/info1/extra"),
        ("case", b"/TEST/INFO1"),
    ]
    return {
        "id": "inspector.core", "program": "server_inspector",
        "ready": {"kind": "tcp", "port": HTTP_PORT},
        "steps": [http_case(name, get(path),
                            "core" if name in ("help", "info1", "info2", "info3")
                            else "malformed",
                            name not in ("help", "info1", "info2", "info3"),
                            prefix="insp")
                  for name, path in paths],
    }


def _inspector_proc():
    """The built-in /proc category reads live process statistics.

    The numbers move between runs by construction, so the body's digits and
    the Content-Length that follows from them are collapsed. The status line,
    the header set and the body's shape stay exact.

    /proc/status needs one narrowing beyond that. Its body is whatever the
    running kernel chooses to report about the process on the machine it is
    running on, most of which is not about ise at all, and its fixed-column
    alignment carries the digit counts the digit collapse had just erased. Only
    the fields every Linux has reported for twenty years are kept, and only for
    their names and order -- see engine.PROC_STATUS_FIELDS (NOTES B19).
    """
    paths = ["/proc/basic_info", "/proc/opened_file_count", "/proc/status",
             "/proc/thread_count"]
    return {
        "id": "inspector.proc", "program": "server_inspector",
        "ready": {"kind": "tcp", "port": HTTP_PORT},
        "steps": [http_case(path.rsplit("/", 1)[1], get(path.encode()),
                            "core", False, prefix="insp",
                            norm={"http_volatile": True,
                                  "proc_status": path.endswith("/status")})
                  for path in paths],
    }


def _udp_core():
    return {
        "id": "udp.core", "program": "udp_server",
        "ready": {"kind": "udp", "port": UDP_PORT},
        "steps": shutdown_settled([
            udp("udp/hello", "core", False, udp_packet(AC_HELLO, 1, body=b"Hello!")),
            udp("udp/hello_empty", "core", False, udp_packet(AC_HELLO, 2, body=b"")),
            udp("udp/hello_again", "state", True,
                udp_packet(AC_HELLO, 3, body=b"second")),
            udp("udp/ack_to_server", "malformed", True,
                udp_packet(AC_ACK, 4, body=b""), expect=1),
            # See hello/second_instance: the guard is the framework's, and a
            # server that never opens a listening socket is the case most
            # likely to lose it quietly.
            {"id": "udp/second_instance", "stratum": "lifecycle", "adv": True,
             "op": "spawn", "args": ["--nodaemon"], "timeout": 25},
            udp("udp/after_refusal", "lifecycle", True,
                udp_packet(AC_HELLO, 5, body=b"still here")),
        ]),
    }


def _udp_numeric():
    """Action codes and length prefixes at their boundaries.

    checkUnsafeSize takes a signed int but the length prefix on the wire is
    unsigned, so 0x80000000 and 0xFFFFFFFF arrive as negative values and
    0x00800000 is the last accepted size. Each of those is a fixture.
    """
    steps = []
    for name, action in [("zero", 0), ("one", 1), ("below", AC_HELLO - 1),
                         ("above", AC_HELLO + 1), ("ack", AC_ACK),
                         ("int_max", 0x7FFFFFFF), ("sign_bit", 0x80000000),
                         ("all_ones", 0xFFFFFFFF)]:
        steps.append(udp("udpn/action_%s" % name, "numeric", True,
                         udp_packet(action, 9, body=b"probe")))
    for name, size in [("len_zero", 0), ("len_one", 1),
                       ("len_int_max", 0x7FFFFFFF), ("len_sign_bit", 0x80000000),
                       ("len_all_ones", 0xFFFFFFFF), ("len_8m", EIGHT_MB),
                       ("len_8m_plus1", EIGHT_MB + 1)]:
        steps.append(udp("udpn/%s" % name, "numeric", True,
                         udp_packet(AC_HELLO, 10, size_field=size, body=b"ab")))
    # A declared length that is one byte longer than the body, and one shorter.
    steps.append(udp("udpn/len_over_by_one", "numeric", True,
                     udp_packet(AC_HELLO, 11, size_field=6, body=b"abcde")))
    steps.append(udp("udpn/len_under_by_one", "numeric", True,
                     udp_packet(AC_HELLO, 12, size_field=4, body=b"abcde")))
    # Sequence number zero: the ack echoes it back, and zero is the value
    # init() treats as "allocate one for me" on the sending side.
    steps.append(udp("udpn/seq_zero", "numeric", True,
                     udp_packet(AC_HELLO, 0, body=b"s")))
    steps.append(udp("udpn/seq_all_ones", "numeric", True,
                     udp_packet(AC_HELLO, 0xFFFFFFFF, body=b"s")))
    return {
        "id": "udp.numeric", "program": "udp_server",
        "ready": {"kind": "udp", "port": UDP_PORT},
        "steps": shutdown_settled(steps),
    }


def _udp_malformed():
    cases = [
        ("empty", b""),
        ("one_byte", b"\x01"),
        ("seven_bytes", b"\x00" * 7),
        ("header_only", struct.pack("<II", AC_HELLO, 5)),
        ("header_plus_3", struct.pack("<II", AC_HELLO, 5) + b"\x00\x00\x00"),
        ("all_ff_16", b"\xff" * 16),
        ("text", b"GET / HTTP/1.1\r\n\r\n"),
        ("trailing_junk", udp_packet(AC_HELLO, 6, body=b"hi") + b"JUNKJUNK"),
    ]
    steps = [udp("udpm/%s" % name, "malformed", True, payload)
             for name, payload in cases]
    return {
        "id": "udp.malformed", "program": "udp_server",
        "ready": {"kind": "udp", "port": UDP_PORT},
        "steps": shutdown_settled(steps),
    }


def _udp_width():
    steps = []
    for size in (1, 1023, 1024, 1025, 8192, 60000):
        steps.append(udp("udpw/body_%d" % size, "width", True,
                         udp_packet(AC_HELLO, 20, body=b"w" * size)))
    # Past the maximum UDP payload the server's receive buffer will take.
    steps.append(udp("udpw/body_65400", "width", True,
                     udp_packet(AC_HELLO, 21, body=b"w" * 65400)))
    steps.append(udp("udpw/text_utf8", "text", True,
                     udp_packet(AC_HELLO, 22,
                                body="héllo 世界 \U0001F600".encode("utf-8"))))
    steps.append(udp("udpw/text_nul", "text", True,
                     udp_packet(AC_HELLO, 23, body=b"a\x00b")))
    return {
        "id": "udp.width", "program": "udp_server",
        "ready": {"kind": "udp", "port": UDP_PORT},
        "steps": shutdown_settled(steps),
    }


def _pair_udp_client():
    return {
        "id": "pair.udp_client", "kind": "pair", "program": "udp_server",
        "ready": {"kind": "udp", "port": UDP_PORT},
        "steps": shutdown_settled([
            {"id": "pair/udp_client", "stratum": "core", "adv": False,
             "op": "client", "program": "udp_client", "args": ["--nodaemon"],
             "timeout": 30},
        ]),
    }


def _pair_chargen_client():
    return {
        "id": "pair.chargen_client", "kind": "pair", "program": "chargen_server",
        "ready": {"kind": "tcp", "port": CHARGEN_PORT},
        "steps": [
            # The client prints every byte it receives and chargen never
            # stops, so this is scored on the first 8 KiB of the stream --
            # which is fixed -- and on the fact that more followed. Both of
            # those are properties of the stream, so the stop condition is one
            # too: read on until twice the kept prefix has arrived, then stop.
            # The four-second window this replaces measured the box instead,
            # and would have failed a correct build on a busy one.
            {"id": "pair/chargen_client", "stratum": "core", "adv": False,
             "op": "client", "program": "chargen_client",
             "args": ["127.0.0.1", "--nodaemon"],
             "cap": 8192, "until_bytes": 16384, "timeout": 120},
        ],
        # How many chunks arrived before the cap is a function of the machine;
        # which lines the client writes is not.
        "norm_log": {"unique": True, "digits": True},
        "norm_stdout": {"unique": True, "digits": True},
    }


def _cli_noargs():
    """chargen_client with no arguments prints its usage and stops.

    Only this example refuses to start without an argument; every other one
    would daemonise, which is covered by the lifecycle fixtures instead.
    """
    return {
        "id": "cli.noargs", "kind": "cli",
        "steps": [{"id": "cli/chargen_client/noargs", "stratum": "malformed",
                   "adv": True, "program": "chargen_client", "args": [],
                   "timeout": 15}],
    }


def _timer():
    """The timer example, scored for its lifecycle and not for its timers.

    Its executeAt(+3s), executeAfter(+5s) and executeEvery(1s) callbacks do not
    run when they are due, and whether they run at all inside any window is a
    function of where in a wall second the process happened to start: three
    runs over 130 seconds each logged none of them until SIGTERM arrived, and
    six runs over 22 seconds logged four with every callback at +1s and two
    with none. The cause is Timestamp::now() adding tv_usec, microseconds, to a
    millisecond count (NOTES-authoring.md B18), and it is a defect the baseline
    has, not a race the fixture can wait out.

    So the three callback lines are dropped and what remains -- the app starts,
    says so on stdout and in the log, takes a signal and shuts down in order --
    is scored exactly. TimerQueue's own expiry logic is covered instead by the
    driver's timer_queue_* cases, which hand it fixed Timestamps and never
    consult the clock at all.
    """
    return {
        "id": "timer.core", "program": "timer",
        "ready": {"kind": "sleep", "seconds": 0.5},
        "steps": [{"op": "sleep", "seconds": 3.0}],
        "norm_log": {"drop": [r"onTimer\w+ called\."]},
    }


def _thread_pool():
    """Ten threads repeat a task with a one second sleep between runs.

    How many times each ran inside the window is a function of scheduling, so
    the transcript is scored twice over: once for the distinct line format the
    pool produces, and once for the number of threads that produced it. The
    second is the whole of what the first throws away -- `unique` collapses a
    correct run and a run where one thread did all the work to the same single
    line -- and it counts the ids the task itself prints, not the ids the
    runtime handed out; see engine._apply_norm.
    """
    return {
        "id": "threadpool.core", "program": "thread_pool", "args": [],
        "ready": {"kind": "sleep", "seconds": 0.5},
        # Wait for the ten threads, not for four seconds. The pool promises
        # ten; how long the tenth takes to reach its first line is the box's
        # business, and a fixed sleep hands that number to the expectation.
        "steps": [{"op": "await_threads", "count": 10, "timeout": 120}],
        "norm_stdout": {"unique": True, "digits": True},
        "norm_log": {"unique": True, "digits": True},
        "extra_items": [
            {"suffix": "threads", "from": "stdout", "norm": {"thread_count": True}},
        ],
    }


def _module_msgs():
    return {
        "id": "modmsgs.core", "program": "server_module_msgs", "args": [],
        "ready": {"kind": "sleep", "seconds": 0.5},
        "steps": [{"op": "sleep", "seconds": 4.0}],
        "norm_stdout": {"sort": True},
    }


def _cli(name, args, stratum, adv, timeout=15):
    return {
        "id": "cli.%s" % name, "kind": "cli",
        "steps": [{"id": "cli/%s/%s" % (exe, name), "stratum": stratum,
                   "adv": adv, "program": exe, "args": list(args),
                   "timeout": timeout}
                  for exe in EXES],
    }


# --------------------------------------------------------------------------
# the characterisation driver
#
# The examples reach the servers, the event loop and the logger. They do not
# reach most of ise/main -- the string and number conversions, the streams, the
# containers, the URL and packet codecs, the HTTP header objects, the exception
# hierarchy. isedrv does, by calling them directly; it is built by the verifier
# against the tree under /app and is never copied into /app, so the agent being
# scored never sees it.
#
# One scored item per `### <name>` block it prints. This table is the whole
# declaration of which blocks those are, and check_driver_catalog.py fails if
# it and the driver sources ever disagree, so a case cannot be added to the
# driver and quietly left unscored, or classified twice, or deleted here and
# left running there.
#
# The three cases the driver marks unscored are absent by construction: they
# name a defect whose output is not a function of its inputs (NOTES B8, B11,
# B17), the driver prints `### unscored: <reason>` under their header, and the
# engine refuses to score any block that carries that marker even if one were
# listed.

DRIVER_CASES = (
    # name                                              stratum      adv
    ("classes_atomic_int",                              "numeric",   True),
    ("classes_buffer_binary_roundtrip",                 "text",      True),
    ("classes_buffer_copy_and_assign",                  "core",      False),
    ("classes_buffer_file_roundtrip",                   "core",      False),
    ("classes_buffer_lifecycle",                        "core",      False),
    ("classes_buffer_position_clamp",                   "numeric",   True),
    ("classes_buffer_zero_and_negative_sizes",          "numeric",   True),
    ("classes_datetime_arithmetic_and_compare",         "core",      False),
    ("classes_datetime_decode_fixed",                   "core",      False),
    ("classes_datetime_encode_is_a_function_of_its_inputs", "state", True),
    ("classes_datetime_encode_table",                   "numeric",   True),
    ("classes_datetime_from_string",                    "malformed", True),
    ("classes_datetime_now_shape",                      "core",      False),
    ("classes_object_list_edges",                       "malformed", True),
    ("classes_object_list_non_owning",                  "state",     True),
    ("classes_object_list_owning",                      "state",     True),
    ("classes_seq_number_alloc",                        "state",     True),
    ("classes_timestamp_arithmetic",                    "numeric",   True),
    ("classes_timestamp_fixed",                         "core",      False),
    ("classes_timestamp_now_shape",                     "core",      False),
    ("containers_pointer_list_basics",                  "core",      False),
    ("containers_pointer_list_duplicates_and_null",     "malformed", True),
    ("containers_pointer_list_move_and_resize",         "state",     True),
    ("containers_property_list_basics",                 "core",      False),
    ("containers_property_list_malformed_input",        "malformed", True),
    ("containers_property_list_quoting",                "text",      True),
    ("containers_property_list_reserved_names",         "malformed", True),
    ("containers_property_list_subscript",              "core",      False),
    ("containers_strings_comma_text",                   "text",      True),
    ("containers_strings_copy_and_equals",              "core",      False),
    ("containers_strings_file_roundtrip",               "core",      False),
    ("containers_strings_name_value",                   "core",      False),
    ("containers_strings_text_roundtrip",               "text",      True),
    ("containers_strlist_basics",                       "core",      False),
    ("containers_strlist_data_pointers",                "core",      False),
    ("containers_strlist_duplicates",                   "state",     True),
    ("containers_strlist_out_of_range",                 "numeric",   True),
    ("containers_strlist_sorting",                      "state",     True),
    ("exceptions_assert_failure",                       "malformed", True),
    ("exceptions_base",                                 "core",      False),
    ("exceptions_derived_types",                        "core",      False),
    ("exceptions_file",                                 "core",      False),
    ("exceptions_handler_matching",                     "core",      False),
    ("exceptions_message_table",                        "core",      False),
    ("exceptions_simple",                               "core",      False),
    ("exceptions_throwing_helpers",                     "core",      False),
    ("http_client_before_request_streams",              "numeric",   True),
    ("http_client_body_edges",                          "malformed", True),
    ("http_client_check_response_header",               "malformed", True),
    ("http_client_connect_refused",                     "state",     True),
    ("http_client_download_file",                       "core",      False),
    ("http_client_download_file_rejects",               "malformed", True),
    ("http_client_get_chunked",                         "core",      False),
    ("http_client_get_content_length",                  "core",      False),
    ("http_client_header_timeout",                      "state",     True),
    ("http_client_options_and_state",                   "core",      False),
    ("http_client_parse_response_header",               "malformed", True),
    ("http_client_parse_then_process",                  "state",     True),
    ("http_client_peer_closes_silently",                "state",     True),
    ("http_client_post",                                "core",      False),
    ("http_client_redirect_absolute",                   "core",      False),
    ("http_client_redirect_disabled",                   "state",     True),
    ("http_client_redirect_relative",                   "state",     True),
    ("http_client_relative_redirect_host",              "state",     True),
    ("http_client_request_file_and_receive",            "core",      False),
    ("http_client_url_handling",                        "malformed", True),
    ("http_entity_header_build",                        "core",      False),
    ("http_entity_header_content_length_parsing",       "malformed", True),
    ("http_entity_header_content_range",                "malformed", True),
    ("http_entity_header_defaults",                     "core",      False),
    ("http_entity_header_parse",                        "core",      False),
    ("http_header_list_add_strings_and_move",           "state",     True),
    ("http_header_list_basics",                         "core",      False),
    ("http_header_list_copy",                           "core",      False),
    ("http_header_list_lookup",                         "core",      False),
    ("http_header_list_name_parsing",                   "malformed", True),
    ("http_header_list_set_value",                      "state",     True),
    ("http_parse_request_line",                         "malformed", True),
    ("http_proto_version_strings",                      "core",      False),
    ("http_request_buffer",                             "core",      False),
    ("http_request_header_build_order",                 "state",     True),
    ("http_request_header_info",                        "core",      False),
    ("http_response_buffer",                            "core",      False),
    ("http_response_content_stream_ownership",          "state",     True),
    ("http_response_header_info",                       "core",      False),
    ("http_response_keep_alive",                        "text",      True),
    ("http_response_status",                            "core",      False),
    ("http_set_connection_overload",                    "core",      False),
    ("http_status_messages",                            "numeric",   True),
    ("packet_defaults_and_empty",                       "numeric",   True),
    ("packet_roundtrip",                                "core",      False),
    ("packet_string_and_blob_sizes",                    "width",     True),
    ("packet_truncated_and_corrupt",                    "malformed", True),
    ("socket_error_messages",                           "core",      False),
    ("socket_inet_address",                             "core",      False),
    ("socket_ip_roundtrip",                             "numeric",   True),
    ("socket_ip_to_string",                             "numeric",   True),
    ("socket_network_init",                             "lifecycle", True),
    ("socket_sockaddr_roundtrip",                       "numeric",   True),
    ("socket_string_to_ip",                             "malformed", True),
    ("streams_file_create_and_read",                    "core",      False),
    ("streams_file_large_roundtrip",                    "width",     True),
    ("streams_file_open_failures",                      "malformed", True),
    ("streams_file_set_size_and_append",                "width",     True),
    ("streams_memory_basics",                           "core",      False),
    ("streams_memory_copy",                             "core",      False),
    ("streams_memory_files",                            "core",      False),
    ("streams_memory_growth",                           "width",     True),
    ("streams_memory_read",                             "core",      False),
    ("streams_memory_seek",                             "numeric",   True),
    ("streams_memory_set_size",                         "width",     True),
    ("sysutils_app_paths_shape",                        "core",      False),
    ("sysutils_bool_to_str",                            "core",      False),
    ("sysutils_fetch_str",                              "text",      True),
    ("sysutils_file_lifecycle",                         "lifecycle", False),
    ("sysutils_find_files",                             "core",      False),
    ("sysutils_float_to_str",                           "numeric",   True),
    ("sysutils_format_string",                          "text",      True),
    ("sysutils_int_to_str",                             "numeric",   True),
    ("sysutils_is_bool_str",                            "malformed", True),
    ("sysutils_is_float_str",                           "malformed", True),
    ("sysutils_is_int64_str",                           "malformed", True),
    ("sysutils_is_int_str",                             "malformed", True),
    ("sysutils_path_strings",                           "text",      True),
    ("sysutils_quoted_str",                             "text",      True),
    ("sysutils_random_list_is_a_permutation",           "state",     True),
    ("sysutils_replace_string",                         "text",      True),
    ("sysutils_same_text_and_compare",                  "text",      True),
    ("sysutils_split_string",                           "text",      True),
    ("sysutils_split_string_to_int",                    "malformed", True),
    ("sysutils_str_to_bool",                            "malformed", True),
    ("sysutils_str_to_float",                           "numeric",   True),
    ("sysutils_str_to_int",                             "numeric",   True),
    ("sysutils_str_to_int64",                           "numeric",   True),
    ("sysutils_sys_error_messages",                     "core",      False),
    ("sysutils_thousand_sep",                           "numeric",   True),
    ("sysutils_tick_diff",                              "numeric",   True),
    ("sysutils_trim_case",                              "text",      True),
    # Timer and TimerQueue, driven with fixed Timestamps rather than the clock.
    # examples/timer cannot cover any of this; see NOTES B18.
    # the concurrency half of ise/main, which the examples run and never
    # observe: see driver/c_threads.cpp for why these exist and how each one
    # is made a function of its inputs rather than of the scheduler.
    ("threads_atomic_contention",                       "state",     True),
    ("threads_condition_handoff",                       "state",     True),
    ("threads_mutex_excludes",                          "state",     True),
    ("threads_mutex_recursive",                         "core",      False),
    ("threads_semaphore_counts",                        "core",      False),
    ("threads_semaphore_gates_a_thread",                "state",     True),
    ("threads_singleton_initialises_once",              "state",     True),
    ("threads_thread_interrupt_sleep",                  "state",     True),
    ("threads_thread_lifecycle",                        "core",      False),
    ("threads_thread_pool_runs_every_task",             "state",     True),
    ("threads_thread_scheduling_is_clamped",            "numeric",   True),
    ("threads_thread_terminate_ends_sleep",             "state",     True),
    ("threads_tick_unit",                               "numeric",   True),
    ("timer_ids_are_distinct_and_ascending",            "state",     True),
    ("timer_object_fields",                             "numeric",   True),
    ("timer_queue_cancel_before_expiry",                "state",     True),
    ("timer_queue_cancel_expiring_sibling",             "state",     True),
    ("timer_queue_cancel_from_callback",                "state",     True),
    ("timer_queue_cancel_repeating",                    "state",     True),
    ("timer_queue_destructor_drops_pending",            "lifecycle", True),
    ("timer_queue_expiry_boundary",                     "numeric",   True),
    ("timer_queue_fires_in_time_order",                 "core",      False),
    ("timer_queue_nearest",                             "core",      False),
    ("timer_queue_repeating",                           "state",     True),
    ("timer_queue_same_expiration",                     "state",     True),
    ("timer_queue_self_cancel_from_callback",           "state",     True),
    ("timer_restart",                                   "numeric",   True),
    ("url_parse_malformed",                             "malformed", True),
    ("url_parse_well_formed",                           "core",      False),
    ("url_parts_and_assembly",                          "state",     True),
)

# The blocks the driver prints but nothing may score, and why. Listed so the
# set is reviewable here and not only in C++; the driver prints its own marker
# and the engine obeys that, so this is a cross-check rather than the rule.
DRIVER_UNSCORED = (
    "classes_datetime_encode_indeterminate",
    "http_response_content_range_defect",
    "packet_unpack_zero_length_buffer",
)


def _driver():
    return {
        "id": "driver", "kind": "driver", "program": "isedrv",
        "args": ["--nodaemon"], "timeout": 300,
        "steps": [{"id": "driver/%s" % name, "stratum": stratum, "adv": adv,
                   "case": name}
                  for name, stratum, adv in DRIVER_CASES],
    }


def sessions():
    """Every session, in the order the recorder runs them."""
    return [
        _driver(),
        _echo_core(), _echo_text(), _echo_width(), _echo_state(),
        _echo_lifecycle(),
        _maxconn_echo(), _maxconn_cap(),
        _discard(), _daytime(), _chargen(),
        _fourinone(), _hello(), _hello_daemon(),
        _http_core(), _http_malformed(), _http_width(),
        _http_lifecycle(), _http_daemon(),
        _inspector(), _inspector_proc(),
        _udp_core(), _udp_numeric(), _udp_malformed(), _udp_width(),
        _pair_udp_client(), _pair_chargen_client(),
        _timer(), _thread_pool(), _module_msgs(),
        _cli("version", ["--version"], "core", False),
        _cli("help", ["--help"], "core", False),
        _cli_noargs(),
    ]


def all_steps():
    for session in sessions():
        for step in session["steps"]:
            if "id" in step:
                yield session, step
