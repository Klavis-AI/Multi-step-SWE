#!/usr/bin/env python3
"""Scored-case runner, shared by every step of pdf-library-version-chain.

Reads $TESTS/cases.json (the scored set) and drives the candidate's utilities,
appending one "<group> <passed> <total>" line per group to $LOG/groups.txt.

Nothing the candidate wrote is trusted: every expectation comes from $TESTS, which
test.sh has already made root-only and fingerprinted. The PDFs themselves are read
from $CORPUS_DIR, a world-readable copy, because the candidate's binaries run under an
unprivileged uid and must be able to open them while the expectations stay unreadable.

Case kinds
----------
Milestone 1 (pdf13-core):
info      run `pdfinfo ARGS FILE`, compare the scored field subset of stdout
text      run `pdftotext ARGS FILE -`, compare after normalization
fonts     run `pdffonts ARGS FILE`, compare the normalized table
stream    run `qpdf --show-object=N[,G] --filtered-stream-data FILE`, compare sha256
raw       run `qpdf --show-object=N[,G] --raw-stream-data FILE`, compare sha256
exit      run any of the above, compare only the exit status

Milestone 2 (pdf14-transparency), one per comparison form in TEST_INTERFACE.md 4.1:
pages     `qpdf --show-pages`, compared as a normalized line sequence
xref      `qpdf --show-xref`, compared as a normalized line sequence
object    `qpdf --show-object=...`, whole dump with whitespace runs collapsed
check     `qpdf --check`, only the four scored line forms, wrapped lines joined
linparams `qpdf --show-linearization`, the parameter block alone
lindump   `qpdf --show-linearization`, the full dump minus the per-page hint entries
warn      `qpdf --check`, the set of warning identities over stdout and stderr merged

Milestone 4 (pdf16-17-crypto-forms), one per comparison form in TEST_INTERFACE.md 4.1:
encryption `qpdf --show-encryption`, or the same block printed inline by `--check`
imagelist  `pdfimages -list`, the table minus its header line and dashed rule
codestream `pdfimages -jbig2 FILE ROOT`, the set of file names produced plus each sha256
sig        `pdfsig`, only the scored line forms of the signature report
product    one of the three writing options, then a second utility over the product

Milestones 6 and 7 (pdfa1-pdfa2 / pdfa3-a4-ua), one per comparison form in 4.1:
verdict   ONE batched `verapdf -f FLAVOUR --format text FILE...`. 4.1 makes
          validation a batch, so a batch is not a case: it carries one case per
          operand, plus one for the line set as a whole and one for the batch's
          exit status, which keeps partial credit per file
convert   `gs -dPDFA=N ... -sOutputFile=OUT IN`, judged by running the REAL veraPDF
          over the file it wrote and then by the page count and text of the input

Two operand shapes are new at milestone 4.  `out_operand` appends a scratch output
path after the file (the writing options, and `pdfimages`' IMAGE-ROOT); `product`
runs the writing option first, compares its exit status, and then judges a second
utility run over the file it wrote, by that utility's own rule.  Neither the product
nor the extracted image files are compared byte for byte: 4.1 makes object numbering
in a product free, and removes decoded pixels from scoring entirely.

Every case carries its own `expect`; nothing is derived here at scoring time, and the
recorded `expect_exit` is compared for every case regardless of kind.

The file operand
----------------
Every expectation in $TESTS was recorded by running the reference tool with the corpus
file's BARE BASENAME as the operand, and qpdf echoes that operand verbatim -- into
`checking FILE`, `FILE is not linearized`, `FILE: linearization data:` and each
`WARNING: FILE: ...` prefix. This runner must pass a path, so the echoed operand is
made path-independent before comparison; see strip_operand_path().

Milestone 6/7's `verdict` kind is the deliberate exception: 4.1 fixes its operand as
an ABSOLUTE path echoed back byte for byte, and the scorer is the thing that builds
that path, so the same string stands on both sides and NOTHING is rewritten. See the
note beside PATH_KINDS.
"""

import concurrent.futures as futures
import errno
import hashlib
import json
import os
import re
import shutil
import stat
import subprocess
import sys
import tempfile
import time
import unicodedata

TESTS = os.environ.get("TESTS", "/tests")
CORPUS_DIR = os.environ.get("CORPUS_DIR", "/run/corpus")
BUILD = os.environ.get("CAND_BUILD", "/app/build")
SCORE_UID = os.environ.get("SCORE_UID", "65534")

# The candidate's PATH is its own build directory and nothing else. The reference PDF
# tools are not installed in this image at all, but the image does carry python3 for
# the scorer itself -- and a twenty-line C program that execs an interpreter would
# otherwise satisfy "written in C and built by make". With nothing else on PATH there
# is no interpreter for a candidate binary to reach by name.
CAND_PATH = BUILD

_SETPRIV = shutil.which("setpriv")
if _SETPRIV is None:
    # Same reasoning as test.sh: a conditional drop that silently vanishes would run
    # every candidate binary as root. Refuse rather than downgrade.
    raise SystemExit("FATAL: setpriv unavailable; refusing to run candidate binaries as root")
_LAUNCH = os.environ.get("CAND_LAUNCH", "")
if not (_LAUNCH and os.path.isabs(_LAUNCH) and os.access(_LAUNCH, os.X_OK)):
    raise SystemExit("FATAL: verifier launcher unavailable; refusing to run candidate binaries")
_WS = re.compile(r"\s+")
_CTRL = re.compile(r"[\x00-\x08\x0b\x0c\x0e-\x1f\x7f]")

# --- milestone 2 comparison constants (TEST_INTERFACE.md 4.1) ---------------
LIN_PARAM_KEYS = ["file_size", "first_page_object", "first_page_end", "npages",
                  "xref_zero_offset", "first_page", "H_offset", "H_length"]
CHECK_JOINED = ("No syntax or stream encoding errors found; the file may still "
                "contain errors that qpdf cannot detect")
# 4.1 check: "Wrapped lines are joined". qpdf hard-wraps that sentence and different
# builds wrap it in different places, so the sentence is matched with any whitespace
# run between any two of its words and rewritten to the one-line form. The pattern is
# the sentence and nothing else, so joining can only ever affect the sentence.
CHECK_SENTENCE = re.compile(r"\s+".join(re.escape(w) for w in CHECK_JOINED.split(" ")))
CHECK_LEADS = ("checking ", "PDF Version:", "No syntax or stream encoding errors found;")
CHECK_EXACT = ("File is linearized", "File is not linearized")
# 4.1 linearization dump: a top-level "Page N:" line opens a per-page hint-table block.
PAGE_BLOCK = re.compile(r"^Page \d+:\s*$")
_DIGITS = re.compile(r"\d+")


def _text(s):
    if isinstance(s, bytes):
        return s.decode("utf-8", "replace")
    return s


def collapse(s):
    """One whitespace run -> one space, leading and trailing whitespace removed."""
    return _WS.sub(" ", s).strip()


def normalize_text(s):
    """Comparison form for extracted text: the character sequence, not the layout.

    Two correct extractors disagree about where spaces and line breaks fall, which is
    a layout heuristic rather than a conformance question. They do agree on which
    characters the page contains and in what order, so that is what is compared. A
    wrong glyph, a missing ligature or a bad /ToUnicode still fails.
    """
    if isinstance(s, bytes):
        s = s.decode("utf-8", "replace")
    s = unicodedata.normalize("NFC", s)
    s = s.replace("­", "")
    s = _CTRL.sub(" ", s)
    return _WS.sub(" ", s).strip()


def normalize_table(s):
    if isinstance(s, bytes):
        s = s.decode("utf-8", "replace")
    rows = []
    for line in s.splitlines():
        line = line.strip()
        if not line or set(line) <= set("- "):
            continue
        rows.append(_WS.sub(" ", line))
    return "\n".join(rows)


def info_fields(s, keys):
    if isinstance(s, bytes):
        s = s.decode("utf-8", "replace")
    got = {}
    for line in s.splitlines():
        if ":" in line:
            k, v = line.split(":", 1)
            got.setdefault(k.strip(), _WS.sub(" ", v.strip()))
    return "\n".join(f"{k}: {got[k]}" for k in keys if k in got)


def strip_operand_path(s, operand, basename):
    """Make an echoed file operand path-independent.

    The expectations were captured with a bare basename as the operand; this runner
    passes CORPUS_DIR/basename. Without this, the 108 check / lindump / warn cases
    would all fail on the directory prefix alone -- a harness artefact that looks
    exactly like a broken candidate.

    Two rewrites, both confined to the directory prefix sitting directly in front of
    an occurrence of this case's own basename:

      1. the operand exactly as this runner spelled it, wherever it appears;
      2. any remaining occurrence of the basename preceded by a path prefix -- a run
         of non-whitespace ending in "/".

    (1) alone would only work for a tool that echoes our argv verbatim. (2) is what
    makes the rule hold when the candidate's own binary spells the operand some other
    way -- "./NAME", a ".."-relative path, a resolved symlink, a path this runner
    never constructed.

    Neither rewrite can mask a difference elsewhere: the basename itself is never
    altered, the match must end exactly at the basename, nothing without a "/"
    immediately before the basename is touched, a basename embedded in a longer token
    is not matched, and an operand naming some other file keeps its whole path and
    still fails. Applied to both sides of the comparison, so the comparison is
    path-independent rather than merely one-sided.
    """
    s = _text(s)
    if operand and operand != basename:
        s = s.replace(operand, basename)
        # (2a) This step's corpus-relative paths contain spaces in BOTH the clause
        # directory and the basename, and r"\S*" cannot cross one. Anchoring on the
        # known relative path leaves \S* to cover only the mount prefix, which has no
        # space. A wildcard prefix here would be wrong: it swallows the leading
        # verdict token out of "FAIL <path> 1b". The prefix is optional so a bare
        # relative path, spelled with no directory in front, also normalizes.
        rel = (operand[len(CORPUS_DIR):].lstrip("/")
               if operand.startswith(CORPUS_DIR) else "")
        if rel and rel != basename:
            s = re.sub(r"(?:\S*/)?" + re.escape(rel), basename, s)
    return re.sub(r"\S*/" + re.escape(basename), basename, s)


def normalize_lines(s):
    """4.1 pages / xref: strip and collapse each line, drop empties, keep order."""
    return "\n".join(c for c in (collapse(l) for l in _text(s).splitlines()) if c)


def normalize_object(s):
    """4.1 object: whitespace runs collapsed over the whole dump, ends stripped.

    Whole-dump rather than per-line, so the reference's line wrapping and the two
    spaces in the "Object is stream. Dictionary:" header do not matter. Key order,
    dropped nulls and dangling references, verbatim reals and surviving escapes are
    all left exactly as the tool printed them -- they are what the case tests.
    """
    return collapse(_text(s))


def normalize_check(s, operand, basename):
    """4.1 check: only the four scored line forms, wrapped lines joined first."""
    s = CHECK_SENTENCE.sub(CHECK_JOINED, strip_operand_path(s, operand, basename))
    out = []
    for line in s.splitlines():
        c = collapse(line)
        if c.startswith(CHECK_LEADS) or c in CHECK_EXACT:
            out.append(c)
    return "\n".join(out)


def normalize_lin_params(s):
    """4.1 linearization dump, parameter block alone: the eight scored keys."""
    got = {}
    for line in _text(s).splitlines():
        m = re.match(r"^([a-zA-Z_]+):\s*(.*)$", line.strip())
        if m and m.group(1) in LIN_PARAM_KEYS and m.group(1) not in got:
            got[m.group(1)] = collapse(m.group(2))
    return "\n".join(f"{k}: {got[k]}" for k in LIN_PARAM_KEYS if k in got)


def normalize_lin_dump(s, operand, basename):
    """4.1 linearization dump, full: everything except the per-page hint entries.

    A top-level "Page N:" line opens the Page Offsets Hint Table's per-page block;
    that line and the indented entries under it are dropped, and the first line that
    is not an indented continuation ends the block and is kept. The parameter block,
    the hint table's own parameters and the whole Shared Objects Hint Table -- whose
    "Shared Object N:" blocks are not per-page blocks -- are all compared.

    The per-page entries are dropped because no field model of ISO 32000-1 Table F.3
    reproduces the reference tool's values for them, so they cannot be scored; the
    expectations in $TESTS were recorded with this same rule applied.
    """
    s = strip_operand_path(s, operand, basename)
    out, skipping = [], False
    for line in s.splitlines():
        if PAGE_BLOCK.match(line.strip()) and not line.startswith(" "):
            skipping = True
            continue
        if skipping:
            if line[:1] in (" ", "\t") and line.strip():
                continue
            skipping = False
        c = collapse(line)
        if c:
            out.append(c)
    return "\n".join(out)


def warning_set(out, err, operand, basename):
    """4.1 warnings: the unordered, de-duplicated set of warning identities.

    The one comparison made over stdout and stderr merged, so it does not depend on
    which stream a warning arrives on. Identity: drop "WARNING:", drop a following
    "FILE: " or "FILE (offset D): " prefix, replace every digit run with "#",
    collapse. The operand path is stripped first, so that a directory prefix's own
    digits never reach the digit rule.
    """
    t = (strip_operand_path(out, operand, basename) + "\n"
         + strip_operand_path(err, operand, basename))
    pre = re.compile(r"^" + re.escape(basename) + r"(?: \(offset \d+\))?: ")
    seen = set()
    for line in t.splitlines():
        c = line.strip()
        if not c.startswith("WARNING:"):
            continue
        c = c[len("WARNING:"):].strip()
        c = pre.sub("", c)
        c = _DIGITS.sub("#", c)
        seen.add(collapse(c))
    return "\n".join(sorted(seen))


# --- milestone 4 comparison constants (TEST_INTERFACE.md 4.1) ----------------
# `encryption`: the block qpdf prints for --show-encryption and inline in --check.
# The three `encryption method:` lines are emitted only when /V >= 4, and their
# ABSENCE is as much a part of the answer as their content, so nothing is
# synthesised here -- the filter keeps what the tool printed, in its order.
ENC_LEADS = ("Incorrect password supplied",
             "R = ", "P = ", "User password =",
             "Supplied password is",
             "extract for accessibility:", "extract for any purpose:",
             "print low resolution:", "print high resolution:",
             "modify document assembly:", "modify forms:", "modify annotations:",
             "modify other:", "modify anything:",
             "stream encryption method:", "string encryption method:",
             "file encryption method:")
# `signature report`: ONLY these line forms are compared.  Signature Validation,
# Not total document signed, the signer certificate fields, Signing Hash Algorithm
# and Signing Time are excluded -- neither image holds a certificate database, so
# validation fails identically for every signature and no case asks whether one is
# valid.
SIG_LEADS = ("Digital Signature Info of:", "Signature #",
             "- Signature Field Name:", "- Signature Type:", "- Signed Ranges:",
             "The signature form field is not signed.")
SIG_NOSIG = re.compile(r"^File '.*' does not contain any signatures$")
# `imagelist`: the column header line and the dashed rule beneath it are ignored.
IMAGELIST_HDR = ("page", "num", "type")


def normalize_encryption(s):
    """4.1 encryption: the encryption block's lines, in order, collapsed.

    Each line of a `--check` report belongs to exactly one rule; this filter and
    normalize_check's partition the report between them.
    """
    out = []
    for line in _text(s).splitlines():
        c = collapse(line)
        if c.startswith(ENC_LEADS):
            out.append(c)
    return "\n".join(out)


def normalize_imagelist(s):
    """4.1 imagelist: header line and dashed rule dropped, rows in order.

    A page carrying no image prints the header and the rule and nothing else, so
    the comparison form is the empty string -- which 4.1 names as a scored answer.
    """
    rows = []
    for line in _text(s).splitlines():
        c = collapse(line)
        if not c or set(c) <= set("- "):
            continue
        if tuple(c.split()[:3]) == IMAGELIST_HDR:
            continue
        rows.append(c)
    return "\n".join(rows)


def normalize_sig(s, operand, basename):
    """4.1 signature report: only the listed line forms, in order, collapsed."""
    s = strip_operand_path(s, operand, basename)
    out = []
    for line in s.splitlines():
        c = collapse(line)
        if not c:
            continue
        if c.startswith(SIG_LEADS) or SIG_NOSIG.match(c):
            out.append(c)
    return "\n".join(out)


# Root reads files the candidate's own programs wrote, in a scratch directory those
# programs could write to (and a process they left behind still can). What is at a
# path when root opens it need not be what was there a moment earlier: a FIFO in its
# place blocks the open, or the read, forever -- and a verifier that never finishes
# drops the whole step from the trial mean -- a device never ends, and a symlink
# points root at a file of the candidate's choosing. So the open neither follows a
# final symlink nor waits, anything that is not a regular file is refused before a
# byte is read, and no more than READ_CAP bytes are read. Every file a correct
# utility writes for a scored case is a page raster or a PDF derived from a corpus
# file of a few kilobytes; the largest, an uncompressed 150 dpi raster, is about
# 1.2 MB, so the cap only turns away output no reference run could have produced.
READ_CAP = 64 << 20


def read_regular(path, cap=READ_CAP):
    """The bytes of `path`, a regular file of at most `cap` bytes; OSError otherwise."""
    fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK | os.O_CLOEXEC)
    try:
        st = os.fstat(fd)
        if not stat.S_ISREG(st.st_mode):
            raise OSError(errno.EINVAL, "not a regular file", path)
        if st.st_size > cap:
            raise OSError(errno.EFBIG, "larger than %d bytes" % cap, path)
        chunks, n = [], 0
        while True:
            b = os.read(fd, min(1 << 20, cap + 1 - n))
            if not b:
                return b"".join(chunks)
            chunks.append(b)
            n += len(b)
            if n > cap:
                raise OSError(errno.EFBIG, "larger than %d bytes" % cap, path)
    finally:
        os.close(fd)


def codestream_set(outdir):
    """4.1 codestream: the set of file names produced plus the SHA-256 of each.

    The root's own basename is "img" in both the recording and here, so the names
    the tool derives from it are already path-independent and nothing is rewritten.
    Sorted, because the comparison is over a set.
    """
    rows = []
    try:
        names = sorted(os.listdir(outdir))
    except OSError:
        return ""
    for fn in names:
        p = os.path.join(outdir, fn)
        if not os.path.isfile(p):
            continue
        try:
            rows.append("%s %s" % (fn, hashlib.sha256(read_regular(p)).hexdigest()))
        except OSError:
            rows.append("%s <unreadable>" % fn)
    return "\n".join(rows)


def _scratch():
    """A world-writable scratch directory: the candidate's binaries run under an
    unprivileged uid and must be able to create their output in it."""
    d = tempfile.mkdtemp(prefix="s4case-", dir="/tmp")
    os.chmod(d, 0o777)
    return d


def run_tool(argv, timeout):
    env = {
        "PATH": CAND_PATH,
        "LC_ALL": "C.UTF-8",
        "HOME": "/tmp",
        "TMPDIR": "/tmp",
        # The task's one frozen-clock hook (reproducible-builds.org).
        "SOURCE_DATE_EPOCH": "1700000000",
    }
    # The verifier's launcher (tests/launch.c, built by test.sh) performs the same
    # drop as setpriv and then lets the utility start no other program and open no
    # inet socket.
    argv = [_LAUNCH, str(SCORE_UID)] + argv
    try:
        # Run from /app, the documented workspace root, so a utility that resolves a
        # data file relative to the working directory behaves here exactly as it did
        # under the agent's own `make && run-public-tests.sh`.
        p = subprocess.run(argv, capture_output=True, timeout=timeout, env=env,
                           cwd="/app", stdin=subprocess.DEVNULL)
        return p.returncode, p.stdout, p.stderr
    except subprocess.TimeoutExpired:
        return -9, b"", b"timeout"
    except FileNotFoundError:
        return -127, b"", b"not built"
    except Exception as exc:  # noqa: BLE001 - a broken candidate scores, never crashes us
        return -1, b"", str(exc).encode()


# The basename every writing option's product is given, in the recording and here
# alike, so that a `product` case's second stage is path-independent for the same
# reason every other operand is: the name on both sides is the same name.
PRODUCT_NAME = "product.pdf"
# The kinds whose RECORDED side is rewritten by strip_operand_path. Every other
# kind compares a normalized actual against the expectation EXACTLY AS STORED,
# which is why the generator stores pre-normalized expectations for all of them.
PATH_KINDS = ("check", "lindump", "warn", "sig")


def _argv_for(spec, path, extra=None):
    """The documented operand order: options, then FILE, then the one trailing
    operand the interface defines for this command (`-` for standard output, an
    OUTFILE for the three writing options, an IMAGE-ROOT for pdfimages)."""
    argv = [os.path.join(BUILD, spec["tool"])] + list(spec.get("args", []))
    argv.append(path)
    if spec.get("stdout_operand"):
        argv.append("-")
    if extra is not None:
        argv.append(extra)
    return argv


def _actual(kind, spec, out, err, path, base, scratch):
    """The normalized comparison form of one run, or None for an unknown kind."""
    if kind == "text":
        return normalize_text(out)
    if kind == "info":
        return info_fields(out, spec["keys"])
    if kind == "fonts":
        return normalize_table(out)
    if kind in ("stream", "raw"):
        return hashlib.sha256(out).hexdigest()
    if kind in ("pages", "xref"):
        return normalize_lines(out)
    if kind == "object":
        return normalize_object(out)
    if kind == "check":
        return normalize_check(out, path, base)
    if kind == "linparams":
        return normalize_lin_params(out)
    if kind == "lindump":
        return normalize_lin_dump(out, path, base)
    if kind == "warn":
        return warning_set(out, err, path, base)
    if kind == "encryption":
        return normalize_encryption(out)
    if kind == "imagelist":
        return normalize_imagelist(out)
    if kind == "sig":
        return normalize_sig(out, path, base)
    if kind == "codestream":
        return codestream_set(scratch)
    return None


# ==========================================================================
# Milestones 6 and 7 (pdfa1-pdfa2 / pdfa3-a4-ua), one per comparison form in
# TEST_INTERFACE.md 4.1.
#
# WHICH SIDE NORMALISES -- read this beside PATH_KINDS above.
#
#   `verdict`  neither side is rewritten. The expectation is stored EXACTLY as the
#              reference tool printed it, collapsed once by the generator through
#              collapse() -- the same function applied here to the candidate's
#              stdout. 4.1 fixes the operand as an ABSOLUTE path echoed back byte
#              for byte, and the scorer builds that path as CORPUS_DIR + the case's
#              relative file name, so the identical string stands on both sides and
#              no rewrite is needed or wanted. strip_operand_path() must NOT be
#              applied: it would erase the very directory prefix this group scores.
#              A guard below proves the assumption rather than trusting it -- if
#              CORPUS_DIR ever stops matching the recorded root, the case reports
#              HARNESS instead of silently reading as a wrong candidate.
#
#   `convert`  no stored output at all beyond the INPUT's own page count and text,
#              both stored pre-normalised through info_fields()/normalize_text() --
#              the same functions the earlier milestones' kinds use, so `convert`
#              sits on the "expectation stored pre-normalised" side with every kind
#              outside PATH_KINDS. 4.1 forbids comparing the produced file itself.
# ==========================================================================

# The independent conformance checker used to judge what the candidate's `gs` wrote.
# 4.1: "OUT is run through the real veraPDF 1.30.2, not through your verapdf". It is
# installed in the verifier image; the candidate cannot reach it (its PATH is
# /app/build alone) and never runs it -- only this scorer does.
REF_VERAPDF = os.environ.get("REF_VERAPDF", "/opt/verapdf/verapdf")

# The basename every conversion product is given, here and in the recording alike.
CONVERT_NAME = "converted.pdf"

_STREAM = re.compile(rb"stream\r?\n(.*?)endstream", re.S)


def normalize_verdict_lines(s):
    """4.1 verdict lines: whitespace runs collapsed to one space, empties dropped.

    Order is deliberately preserved here; the comparison below sorts. Nothing else
    is touched -- the operand path, including its spaces, is part of the answer.
    """
    return [c for c in (collapse(l) for l in _text(s).splitlines()) if c]


def run_ref(argv, timeout):
    """Run a VERIFIER-OWNED tool. No privilege drop and no candidate PATH: this is
    the scorer's own reference checker, not something the candidate supplied."""
    # A full system PATH, unlike run_tool's: the reference checker is the verifier's
    # own program and its launcher shells out to dirname/uname/which/java. This PATH
    # is never visible to the candidate, whose own PATH stays /app/build alone.
    env = {"PATH": "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin",
           "LC_ALL": "C.UTF-8", "HOME": "/tmp", "TMPDIR": "/tmp"}
    try:
        p = subprocess.run(argv, capture_output=True, timeout=timeout, env=env,
                           stdin=subprocess.DEVNULL)
        return p.returncode, p.stdout, p.stderr
    except subprocess.TimeoutExpired:
        return -9, b"", b"timeout"
    except FileNotFoundError:
        return -127, b"", b"reference checker not installed"
    except Exception as exc:  # noqa: BLE001
        return -1, b"", str(exc).encode()


def ref_conformant(path, flavour, timeout, attempts=3):
    """(ok, detail) from the REAL veraPDF over one file at one flavour.

    An EMPTY stdout is not a verdict. The reference checker is a JVM launched from a
    shell script, and on a loaded machine it can fail to start, exiting 1 with
    nothing on stdout for a product that validates PASS when run again. Reading that
    as "not conformant" would charge a candidate for the
    scorer's own infrastructure, so a missing verdict is RETRIED, and a persistent
    one is reported as HARNESS rather than as a candidate failure. A FAIL line is a
    real verdict and is never retried.
    """
    last = ""
    for _ in range(attempts):
        rc, out, err = run_ref([REF_VERAPDF, "-f", flavour, "--format", "text",
                                path], timeout)
        if rc == -127:
            return None, ("HARNESS: reference checker %s is not installed; this "
                          "case cannot be judged" % REF_VERAPDF)
        lines = normalize_verdict_lines(out)
        if lines:
            line = lines[0]
            if line.startswith("PASS "):
                return True, ""
            return False, "reference checker says: %s" % line[:120]
        last = "rc=%s stderr=%r" % (rc, err[:140])
    return None, ("HARNESS: reference checker produced no verdict in %d attempts "
                  "(%s); this case was not judged" % (attempts, last))


def has_structtreeroot(path):
    """Does this file carry a /StructTreeRoot, including inside an object stream?

    Raw scan first; then every stream is inflated and scanned, so a catalogue that
    lives in a compressed object stream is still seen. Standard library only.
    """
    try:
        raw = read_regular(path)
    except OSError:
        return False
    if b"/StructTreeRoot" in raw:
        return True
    import zlib as _z
    for m in _STREAM.finditer(raw):
        blob = m.group(1)
        for cand in (blob, blob.rstrip(b"\r\n")):
            try:
                if b"/StructTreeRoot" in _z.decompressobj().decompress(cand):
                    return True
            except Exception:  # noqa: BLE001 - a stream that is not Flate is skipped
                pass
    return False


def judge_verdict(case, timeout):
    """One batched `verapdf -f FLAVOUR --format text FILE...` -> many scored units.

    4.1 makes validation a BATCH: the whole scored set for a flavour is one
    invocation. A batch is therefore not a case -- it is the vehicle for
    `len(files) + 2` cases, so that partial credit stays per file:

      * one case PER OPERAND. It passes when the operand's recorded line appears in
        the candidate's stdout exactly as often as it was recorded (once), the lines
        having been collapsed on both sides and compared as a set. For an operand
        naming a file that does not exist the recorded line is null and the scored
        property is an ABSENCE: the case passes only when NO line names that path,
        so a program that invents a FAIL line for a file it never opened fails it.
      * one case for the line SET as a whole -- sorted equality, which is what 4.1's
        "the line count must match" catches: an extra line nobody asked for is
        invisible to the per-operand cases above and fails this one.
      * one case for the batch's EXIT STATUS, which 4.1 makes a property of the
        batch and not of any file in it.
    """
    flavour = case["flavour"]
    files = case["files"]
    exp_line = case["expect_line"]
    subids = case["subids"]
    paths = [os.path.join(CORPUS_DIR, f) for f in files]
    argv = ([os.path.join(BUILD, case["tool"]), case.get("flag", "-f"), flavour,
             "--format", "text"] + paths)
    rc, out, err = run_tool(argv, timeout)

    actual = normalize_verdict_lines(out)
    act = {}
    for line in actual:
        act[line] = act.get(line, 0) + 1
    expected = [l for l in exp_line if l is not None]
    exp = {}
    for line in expected:
        exp[line] = exp.get(line, 0) + 1

    # An ABSENCE only means something if the checker actually RAN. A candidate that
    # never built verapdf also prints no line for the absent operand, and would
    # otherwise collect every absence unit for free -- a pass from a check that could
    # not have failed is not evidence. run_tool prefixes argv with the verifier's
    # launcher, which EXISTS, so a missing binary does not raise FileNotFoundError: it
    # returns the launcher's own 127. Testing rc < 0 alone would therefore pass the
    # very case this check exists to catch.
    _tool = os.path.join(BUILD, case["tool"])
    ran = os.access(_tool, os.X_OK) and rc >= 0 and rc != 127
    if not ran:
        # EVERY unit of this batch fails, not just the absence ones. Gating only the
        # absences would leave the line-SET unit open: a case whose single operand is
        # the absent file records an EMPTY expected set, and a tool that never ran
        # prints an empty actual set, so empty == empty and the unit would pass. A
        # batch that did not execute is evidence for nothing in it.
        why = ("%s did not run (executable=%s rc=%s)"
               % (case["tool"], os.access(_tool, os.X_OK), rc))
        return ([(sid, False, why) for sid in subids]
                + [(case["id"] + ":lines", False, why),
                   (case["id"] + ":exit", False, why)])

    res = []
    for i, f in enumerate(files):
        want, p, sid = exp_line[i], paths[i], subids[i]
        if want is None:
            hit = [l for l in actual if p in l]
            res.append((sid, not hit,
                        "" if not hit else
                        "claimed a line for an absent operand: %r" % hit[0][:100]))
            continue
        if p not in want:
            # Verify the check CAN succeed. The expectation embeds the absolute
            # operand this scorer is supposed to be passing; if CORPUS_DIR has moved,
            # every case in the batch would read as a wrong candidate. Say so instead.
            res.append((sid, False,
                        "HARNESS: operand %r is absent from the recorded expectation "
                        "%r -- CORPUS_DIR does not match the recording" % (p, want[:80])))
            continue
        ok = act.get(want, 0) == exp[want]
        res.append((sid, ok, "" if ok else
                    "want %r, seen %d time(s)" % (want[:110], act.get(want, 0))))

    ok_lines = sorted(actual) == sorted(expected)
    res.append((case["id"] + ":lines", ok_lines, "" if ok_lines else
                "%d lines on stdout, expected %d" % (len(actual), len(expected))))
    ok_exit = (rc == case["expect_exit"])
    res.append((case["id"] + ":exit", ok_exit, "" if ok_exit else
                "batch exit %s != %s" % (rc, case["expect_exit"])))
    return res


def judge_convert(case, timeout):
    """4.1 `convert`: gs writes a file, and an INDEPENDENT checker judges it.

    Stages, in order, all of which must hold:
      1. the candidate's `gs` exits 0 and the output exists;
      2. the REAL veraPDF 1.30.2 -- never the candidate's -- calls the output
         compliant at the flavour -dPDFA=N selects;
      3. the document survived: the output's page count and extracted text match
         the INPUT's, as recorded from the reference tools and stored pre-normalised;
      4. (milestone 7 only, and vacuous where the input carries no structure tree)
         a /StructTreeRoot in the input is still in the output, and an input that was
         ua1-compliant is still ua1-compliant.
    The produced file is never compared byte for byte -- 4.1 forbids it.
    """
    scratch = _scratch()
    try:
        src = os.path.join(CORPUS_DIR, case["file"])
        outp = os.path.join(scratch, CONVERT_NAME)
        argv = [os.path.join(BUILD, case["tool"]),
                "-dPDFA=%d" % case["pdfa"], "-dPDFACompatibilityPolicy=1",
                "-dBATCH", "-dNOPAUSE", "-sDEVICE=pdfwrite",
                "-sOutputFile=" + outp, src]
        rc, out, err = run_tool(argv, timeout)
        exp_exit = case.get("expect_exit", 0)
        if rc != exp_exit:
            return False, "gs exit %s != %s (%r)" % (rc, exp_exit, err[:80])
        if not os.path.exists(outp):
            return False, "gs exited %s but wrote no output file" % rc

        ok, detail = ref_conformant(outp, case["flavour"], timeout)
        if ok is None:
            return False, detail
        if not ok:
            return False, "not conformant at %s: %s" % (case["flavour"], detail)

        rc2, out2, err2 = run_tool([os.path.join(BUILD, "pdfinfo"), outp], timeout)
        got_pages = info_fields(out2, ["Pages"])
        if got_pages != case["expect_pages"]:
            return False, ("page count lost: %r != %r"
                           % (got_pages, case["expect_pages"]))

        rc3, out3, err3 = run_tool([os.path.join(BUILD, "pdftotext"), "-enc",
                                    "UTF-8", outp, "-"], timeout)
        got_text = normalize_text(out3)
        if got_text != case["expect_text"]:
            return False, ("text lost: got %r want %r"
                           % (got_text[:70], case["expect_text"][:70]))

        if case.get("check_struct") and not has_structtreeroot(outp):
            return False, "input carried /StructTreeRoot; the output does not"
        if case.get("check_ua1"):
            ok4, d4 = ref_conformant(outp, "ua1", timeout)
            if ok4 is None:
                return False, d4
            if not ok4:
                return False, "input was ua1-compliant, the output is not: %s" % d4
        return True, ""
    finally:
        shutil.rmtree(scratch, ignore_errors=True)


def judge(case, timeout):
    """Returns a LIST of (unit_id, passed, detail). Never raises.

    A list rather than a bool because 4.1 makes one batched verapdf invocation carry
    many scored units; every other kind returns a one-element list, and main() does
    not care which is which. `units` in the case records how many the case must
    yield, and a disagreement is reported rather than silently changing a
    denominator.
    """
    kind = case["kind"]
    if kind == "verdict":
        return judge_verdict(case, timeout)
    if kind == "convert":
        ok, detail = judge_convert(case, timeout)
        return [(case["id"], ok, detail)]
    ok, detail = judge_single(case, timeout)
    return [(case["id"], ok, detail)]


def judge_single(case, timeout):
    """The earlier milestones' kinds, unchanged. Returns (passed, detail)."""
    kind = case["kind"]
    base = case["file"]
    path = os.path.join(CORPUS_DIR, base)
    scratch = None
    try:
        if kind == "product":
            scratch = _scratch()
            prod = os.path.join(scratch, PRODUCT_NAME)
            rc, out, err = run_tool(_argv_for(case, path, prod), timeout)
            exp = case.get("expect_exit")
            if exp is not None and rc != exp:
                return False, f"producer exit {rc} != {exp}"
            if rc < 0:
                return False, f"producer rc={rc} {err[:60]!r}"
            if not os.path.exists(prod):
                return False, "producer wrote no file"
            then = case["then"]
            tkind = then["kind"]
            rc2, out2, err2 = run_tool(_argv_for(then, prod), timeout)
            exp2 = then.get("expect_exit")
            if exp2 is not None and rc2 != exp2:
                return False, f"consumer exit {rc2} != {exp2}"
            if tkind == "exit":
                return True, ""
            if rc2 < 0:
                return False, f"consumer rc={rc2} {err2[:60]!r}"
            got = _actual(tkind, then, out2, err2, prod, PRODUCT_NAME, scratch)
            if got is None:
                return False, f"unknown then-kind {tkind}"
            want = case["expect"]
            if tkind in PATH_KINDS:
                want = strip_operand_path(want, prod, PRODUCT_NAME)
            if got == want:
                return True, ""
            return False, (f"product/{tkind} mismatch: got {got[:80]!r} "
                           f"want {want[:80]!r}")

        extra = None
        if kind == "codestream":
            scratch = _scratch()
            extra = os.path.join(scratch, "img")
        elif case.get("out_operand"):
            scratch = _scratch()
            extra = os.path.join(scratch, PRODUCT_NAME)

        rc, out, err = run_tool(_argv_for(case, path, extra), timeout)

        exp_exit = case.get("expect_exit")
        if exp_exit is not None and rc != exp_exit:
            return False, f"exit {rc} != {exp_exit}"
        if kind == "exit":
            return True, ""
        if rc < 0:
            return False, f"rc={rc} {err[:60]!r}"

        got = _actual(kind, case, out, err, path, base, scratch)
        if got is None:
            return False, f"unknown kind {kind}"

        want = case["expect"]
        if kind in PATH_KINDS:
            want = strip_operand_path(want, path, base)

        if got == want:
            return True, ""
        return False, f"{kind} mismatch: got {got[:80]!r} want {want[:80]!r}"
    finally:
        if scratch:
            shutil.rmtree(scratch, ignore_errors=True)


# --- chance correction (TEST_INTERFACE.md 4.1) -------------------------------
# Every scored flavour carries EQUAL numbers of files the reference calls PASS and
# FAIL, and each batch records exit 1, so a program that prints one token for every
# operand would collect about half of each group without reading a byte of PDF.
# The verdict operands of a group are therefore credited by INFORMEDNESS --
#     max(0, (PASS files called PASS)/(PASS files) + (FAIL files called FAIL)/(FAIL files) - 1)
# which is 1 for a perfect validator and 0 for any constant answer -- and the
# group's other units (the line set, the exit status, absences, unparseable files)
# count only in the same proportion:
#     group fraction = I * (verdict operand units + other units passed) / units.
# `malformed` exists to score unreadable and absent operands, and its few PASS/FAIL
# companions only make each batch mixed, so it uses the informedness pooled over
# every verdict operand of the step instead of its own handful.
SCALE = 1000           # groups.txt carries integers; the fraction is scaled by this
POOLED_GROUPS = ("malformed",)


def informedness(c):
    pos_ok, pos_n, neg_ok, neg_n = c
    if not pos_n or not neg_n:
        return None
    return max(0.0, pos_ok / pos_n + neg_ok / neg_n - 1.0)


def tally_verdicts(conf, case, out):
    """Count one verdict case's PASS/FAIL operands into conf[group] (and conf[None],
    the pooled tally), given the units it produced (missing units are failures)."""
    if case.get("kind") != "verdict":
        return
    okmap = {uid: bool(ok) for uid, ok, _ in out}
    for key in (case["group"], None):
        c = conf.setdefault(key, [0, 0, 0, 0])
        for sid, want in zip(case["subids"], case["expect_line"]):
            if not want:
                continue
            if want.startswith("PASS "):
                c[1] += 1
                c[0] += okmap.get(sid, False)
            elif want.startswith("FAIL "):
                c[3] += 1
                c[2] += okmap.get(sid, False)


def main():
    with open(os.path.join(TESTS, "cases.json")) as fh:
        cases = json.load(fh)
    log_dir = os.environ.get("LOG", "/logs/verifier")
    deadline = float(os.environ.get("CASE_DEADLINE", "1200"))
    per_case_timeout = float(os.environ.get("CASE_TIMEOUT", "25"))
    workers = int(os.environ.get("CASE_WORKERS", "7"))

    start = time.time()
    results = {}
    ran = {}
    done = set()
    detail_lines = []
    conf = {}            # group (None = pooled) -> [pos_ok, pos_n, neg_ok, neg_n]
    vunits = {}          # group -> verdict operand units (PASS/FAIL recorded)

    # A batched verdict case is one invocation carrying hundreds of scored units, so
    # the 25 s that suits a single qpdf run would fail a correct-but-unhurried
    # candidate on the clock alone. Each case may name its own budget; the
    # environment's value remains the default for everything that does not.
    def budget(case):
        return float(case.get("timeout", per_case_timeout))

    def work(idx_case):
        idx, case = idx_case
        try:
            out = judge(case, budget(case))
        except Exception as exc:  # noqa: BLE001
            out = [(case.get("id", "?"), False, f"runner error: {exc}")]
        return idx, case, out

    with futures.ThreadPoolExecutor(max_workers=workers) as pool:
        futs = {pool.submit(work, ic): ic[0] for ic in enumerate(cases)}
        for fut in futures.as_completed(futs):
            idx, case, out = fut.result()
            done.add(idx)
            g = case["group"]
            units = int(case.get("units", 1))
            # The case declares how many scored units it carries; if it produced a
            # different number the denominator would drift silently, so the shortfall
            # is counted as failures and named.
            if len(out) != units:
                detail_lines.append(
                    f"HARNESS {g} {case['id']}: produced {len(out)} units, "
                    f"cases.json declares {units}")
                out = list(out) + [(f"{case['id']}:missing{i}", False,
                                    "unit not produced")
                                   for i in range(units - len(out))]
                out = out[:units] if len(out) > units else out
            passed, total = results.get(g, (0, 0))
            npass = sum(1 for _, ok, _ in out if ok)
            results[g] = (passed + npass, total + len(out))
            ran[g] = ran.get(g, 0) + len(out)
            tally_verdicts(conf, case, out)
            for uid, ok, detail in out:
                if not ok:
                    detail_lines.append(f"FAIL {g} {uid}: {detail}")
            if time.time() - start > deadline:
                for f2 in futs:
                    f2.cancel()
                break

    # A case that never ran still counts its declared units against its group, so a
    # verifier that runs out of time scores low rather than scoring nothing.
    for idx, case in enumerate(cases):
        if idx not in done:
            g = case["group"]
            units = int(case.get("units", 1))
            passed, total = results.get(g, (0, 0))
            results[g] = (passed, total + units)
            detail_lines.append(f"UNRUN {g} {case['id']}: deadline ({units} units)")
            tally_verdicts(conf, case, [])

    # Chance correction: see informedness() above. Groups with no verdict operands
    # (none at these milestones) keep their plain unit fraction.
    scored = {}
    inf_lines = []
    for g in sorted(results):
        p, t = results[g]
        c = conf.get(None if g in POOLED_GROUPS else g)
        inf = informedness(c) if c else None
        if inf is None or not t:
            scored[g] = (p, t)
            continue
        own = conf.get(g, [0, 0, 0, 0])
        v_n = own[1] + own[3]                    # this group's verdict operand units
        v_ok = own[0] + own[2]
        other_ok = p - v_ok                      # line set, exit, absences, no-parse
        frac = inf * (v_n + other_ok) / t
        scored[g] = (int(round(frac * t * SCALE)), t * SCALE)
        inf_lines.append(f"{g} informedness={inf:.6f} (PASS {c[0]}/{c[1]}, FAIL {c[2]}/{c[3]}"
                         f"{', pooled' if g in POOLED_GROUPS else ''}) units {p}/{t} "
                         f"-> fraction {frac:.6f}")

    os.makedirs(log_dir, exist_ok=True)
    with open(os.path.join(log_dir, "groups.txt"), "a") as fh:
        for g in sorted(scored):
            p, t = scored[g]
            fh.write(f"{g} {p} {t}\n")
    with open(os.path.join(log_dir, "ran.txt"), "a") as fh:
        for g in sorted(results):
            p, t = results[g]
            fh.write(f"{g} ran={ran.get(g, 0)} counted={t} passed={p}\n")
    with open(os.path.join(log_dir, "failures.txt"), "a") as fh:
        fh.write("\n".join(detail_lines[:4000]) + "\n")
    with open(os.path.join(log_dir, "ran.txt"), "a") as fh:
        for line in inf_lines:
            fh.write(line + "\n")
    for g in sorted(results):
        p, t = results[g]
        print(f"{g}: {p}/{t} (ran {ran.get(g, 0)})")
    for line in inf_lines:
        print(line)
    print(f"elapsed {time.time() - start:.1f}s over {len(cases)} cases", flush=True)


if __name__ == "__main__":
    main()
