"""Normalisation applied identically at recording and at replay.

Everything volatile in an ise transcript is turned into a stable token here,
and nowhere else. The rules are deliberately narrow: each one names the thing
it erases, so a migration that changes a *format* still fails even though the
value it carries was never scored.

Wire bytes are carried through `latin-1`, which is a total, lossless byte <->
codepoint mapping, so binary payloads survive the round trip unchanged.
"""
import re

# [2026-09-16 06:46:13] -> [TS].  The bracket-and-shape is still asserted, so a
# tree that stops writing a timestamp, or writes a different format, fails.
RE_LOG_TS = re.compile(r"\[\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\]")

# (00008|1875658624) -> (PID|T<n>).  The pid is erased outright; each distinct
# thread id becomes T0, T1, ... so "two different threads logged this" stays
# observable while the values do not.
#
# The value itself is `pthread_self()` put through `%05u`, i.e. the low 32 bits
# of a thread-descriptor address: it differs on every run whether the program
# changed or not. Which *label* a thread gets must not depend on the order the
# threads happened to reach the log -- see seed_thread_map below, which is what
# decides that, and engine._finish, which calls it before this runs.
RE_LOG_IDS = re.compile(r"\((\d{1,10})\|(\d{1,20})\)")

# An ephemeral client port in a peer address or a connection name.
# 127.0.0.1:53438 -> 127.0.0.1:EPH, but only above the ephemeral range floor so
# the server's own listening ports (8000, 8080, 10000-10003, 12345) survive.
RE_EPHEMERAL = re.compile(r"\b(\d{1,3}(?:\.\d{1,3}){3}):(\d{4,5})\b")

# DateTime::now().toDateTimeString() in a daytime response body.
RE_BARE_DATETIME = re.compile(r"\b\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\b")

# thread_pool prints "(thread: 3086885232)".
RE_THREAD_FIELD = re.compile(r"\(thread: (\d+)\)")

# A live gauge of how many connections happen to be open at the instant one is
# accepted. Two clients that overlap by a microsecond change it; nothing in the
# framework promises a value. Measured directly: eight repeats of the same
# fixture produced three to five distinct values.
RE_CONN_COUNT = re.compile(r"\(ConnCount: \d+\)")

# How many bytes one read returned, i.e. how the kernel chose to segment the
# stream. The same 64 KiB send was observed arriving as 16384 and as 16303.
RE_READ_SIZE = re.compile(r"Discarded \d+ bytes")

# Every executable the project builds, plus the characterisation driver the
# verifier builds beside them. Named here because the normaliser has to know
# them to erase the directory they were built into; catalog.py imports the
# list rather than keeping a second copy.
EXE_NAMES = (
    "4in1_server", "chargen_client", "chargen_server", "daytime_server",
    "discard_server", "echo_server", "hello_world", "http_server",
    "max_connection", "server_inspector", "server_module_msgs", "thread_pool",
    "timer", "udp_client", "udp_server",
)

# The directory a binary was built into, which the inspector reports as part of
# its own executable path. The basename stays, so "the program can still find
# its own image" is asserted; where that image happens to live is a property of
# the build, not of the code, and crosscheck_build.py found it to be the one
# item in the suite that changed when the same sources were built elsewhere.
#
# Matched by the executable's own name rather than by the legacy build's
# `build/linux/<type>/bin` layout, because from step 1 onward the tree is
# configured with `cmake -S . -B build` and where CMake then puts a target is
# the build system's business. Nothing in the suite asks the agent to
# reproduce a directory name.
RE_EXE_NAME = re.compile(r"(?:%s|isedrv)\b" % "|".join(EXE_NAMES))

# Where a path in an ise transcript ends, walking backwards from the name.
PATH_STOP = set(" \t\r\n\f\v\"'<>,;:()[]{}=|")


def strip_bindir(text):
    """`/app/build/.../bin/echo_server` -> `<BINDIR>/echo_server`.

    A scan rather than one regular expression. The obvious expression --
    `\\S*/` followed by a lookahead for the name -- is quadratic in the length
    of a run of non-space characters, because `\\S*` runs to the end of the run
    at every starting position and then backtracks looking for the slash. The
    transcript carries payloads of 256 KiB with no whitespace in them.
    Measured, before this replaced it: 16 KiB of non-space took 0.34 s,
    `echo.width` took 251 s, and one recording pass sat in it for 13 minutes.

    Scanning from the name instead touches the text a constant number of times
    per occurrence, and occurrences are rare.
    """
    out, pos = [], 0
    for match in RE_EXE_NAME.finditer(text):
        start = match.start()
        if start <= pos:
            continue
        cut = start
        while cut > pos and text[cut - 1] not in PATH_STOP:
            cut -= 1
        if "/" not in text[cut:start]:
            continue
        out.append(text[pos:cut])
        out.append("<BINDIR>/")
        pos = start
    if not out:
        return text
    out.append(text[pos:])
    return "".join(out)

SERVER_PORTS = {"8000", "8080", "10000", "10001", "10002", "10003", "12345"}


def _ephemeral(match):
    host, port = match.group(1), match.group(2)
    if port in SERVER_PORTS:
        return match.group(0)
    return "%s:EPH" % host


def normalize_text(text, thread_map=None, digits=False):
    """Normalise one chunk of transcript text.

    thread_map -- a dict reused across every chunk of a single session so that
                  thread ids are numbered consistently within the session.
    digits     -- also collapse every run of digits to N. Only used by the
                  fixtures that read live process statistics (/proc/*), where
                  the numbers are genuinely volatile but their presence and
                  the surrounding format are not.
    """
    if thread_map is None:
        thread_map = {}

    def thread_token(raw):
        if raw not in thread_map:
            thread_map[raw] = "T%d" % len(thread_map)
        return thread_map[raw]

    text = RE_LOG_TS.sub("[TS]", text)
    text = RE_LOG_IDS.sub(lambda m: "(PID|%s)" % thread_token(m.group(2)), text)
    text = RE_THREAD_FIELD.sub(lambda m: "(thread: %s)" % thread_token(m.group(1)), text)
    text = RE_EPHEMERAL.sub(_ephemeral, text)
    text = RE_BARE_DATETIME.sub("DATETIME", text)
    text = RE_CONN_COUNT.sub("(ConnCount: N)", text)
    text = RE_READ_SIZE.sub("Discarded N bytes", text)
    text = strip_bindir(text)
    if digits:
        text = re.sub(r"\d+", "N", text)
    return text


def normalize_wire(data, thread_map=None, digits=False):
    """Normalise raw bytes received from a socket."""
    return normalize_text(data.decode("latin-1"), thread_map, digits)


# Any already-normalised thread label, wherever it appears.
RE_THREAD_LABEL = re.compile(r"T(\d+)")


def seed_thread_map(texts):
    """Decide each thread's label from what it wrote, not from when.

    First appearance is not a property of the program. `server_module_msgs`
    proved it: its module thread logs "recved server module message" during
    start-up, racing the main thread's "server_module_msgs started.", and three
    recordings of the identical binary split two-to-one over which of the two
    got called T0. Both transcripts are correct; neither is an expectation.

    What *is* a property of the program is the partition -- which lines were
    written by one thread and which by another. So a thread's label is the rank
    of the set of lines it wrote, among all such sets, ordered by their own
    content. Two threads that wrote indistinguishable sets are
    indistinguishable, and are deliberately given the same label rather than an
    arbitrary order: the count of distinct threads survives in `thread_map`,
    which is what the `thread_count` transform reports.

    `texts` is every raw chunk of one session. Returns the map
    normalize_text expects, already fully populated.
    """
    # Normalise once with a throwaway map, so that everything else volatile --
    # timestamps, pids, ephemeral ports, connection counts -- is already a
    # token and cannot make one run's key differ from another's. The labels
    # this pass assigns are the arbitrary ones; they are masked back out below.
    #
    # Only the chunks that name a thread at all are normalised here. The rest
    # are echoed payloads, and this would be the second of three full passes
    # over them for no result: a chunk with no thread id contributes nothing to
    # a map of thread ids.
    scratch = {}
    normalised = [normalize_text(texts[key], scratch)
                  for key in sorted(texts)
                  if RE_LOG_IDS.search(texts[key])
                  or RE_THREAD_FIELD.search(texts[key])]
    inverse = {}
    for raw, label in scratch.items():
        inverse.setdefault(label, []).append(raw)

    lines_of = {}
    for chunk in normalised:
        for line in chunk.split("\n"):
            labels = RE_THREAD_LABEL.findall(line)
            if not labels:
                continue
            # A line naming two threads must not have its key depend on either
            # one's label, or the ordering would define itself.
            masked = RE_THREAD_LABEL.sub("T", line)
            for label in labels:
                lines_of.setdefault("T" + label, []).append(masked)

    keys = {label: tuple(sorted(found)) for label, found in lines_of.items()}
    order = sorted(set(keys.values()))
    out = {}
    for label, key in keys.items():
        for raw in inverse.get(label, []):
            out[raw] = "T%d" % order.index(key)
    # A thread id that appeared only in a chunk with no line structure still
    # needs a label; give it one after the ranked ones, deterministically.
    for label, raws in sorted(inverse.items()):
        for raw in raws:
            out.setdefault(raw, "T%d" % (len(order) + sorted(inverse).index(label)))
    return out


HTTP_CONTENT_LENGTH = re.compile(rb"\r\nContent-Length:\s*(\d+)\r\n", re.I)


def narrow_http(data):
    """Keep the defined part of an HTTP response and drop the undefined tail.

    The baseline's HttpServer writes the whole 64 KiB content stream to the
    socket regardless of Content-Length, so every response carries ~65 KiB of
    memory past the body that the program never initialised. It is visibly
    different between runs (it holds pointers and fragments of earlier
    responses), i.e. neither stable nor defined, which is exactly what rule 7
    says to narrow rather than record.

    The response up to and including Content-Length bytes of body is both, and
    that is what is scored.
    """
    head, sep, body = data.partition(b"\r\n\r\n")
    if not sep:
        return data
    match = HTTP_CONTENT_LENGTH.search(b"\r\n" + head + b"\r\n")
    if not match:
        return data
    want = int(match.group(1))
    return head + sep + body[:want]
