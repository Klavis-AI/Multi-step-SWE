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

Milestone 8 (raster-linearize):
raster    run `pdftoppm ... -singlefile FILE PREFIX`, compare the DECODED raster
rasterset run `pdftoppm ... -f N -l M FILE PREFIX`, assert the file COUNT and
          compare every page's decoded raster
absent    run `pdftoppm ...`; the scored property is that NO file was produced
checklin  run `qpdf --check-linearization FILE`, compared like `check`
linearize run `qpdf --linearize IN OUT`, then judge OUT structurally against
          Annex F and confirm it still holds the same document

Every case carries its own `expect`; nothing is derived here at scoring time, and the
recorded `expect_exit` is compared for every case regardless of kind.

WHY THE RASTER IS DECODED RATHER THAN HASHED AS A FILE
------------------------------------------------------
TEST_INTERFACE.md 4.1: the scored value is the SHA-256 of the decoded raster,
serialized as the ASCII header "RGB8 W H\\n" followed by the samples in row-major
order, three bytes per pixel, red first. The container is explicitly not compared --
the deflate stream, the IDAT count and the scanline filters are properties of the
encoder, not of the page, and this milestone bars linking the encoder the reference
used. Two structural preconditions are checked before anything is hashed: the file
must be a well-formed 8-bit RGB, non-interlaced, non-palettized PNG, and its width
and height must equal the reference's.

The decoder below is written against zlib and the standard library on purpose: this
image deliberately carries no imaging library, and adding one to decode the
candidate's output would put a PDF-capable dependency in the scorer.

WHY THE LINEARIZED PRODUCT IS JUDGED STRUCTURALLY
-------------------------------------------------
There is no reference qpdf in this image and there must not be one: every other group
depends on the candidate being unable to reach a PDF implementation, and an absolute
path defeats a PATH restriction. So Annex F is checked here, in code whose agreement
with qpdf 11.3.0 was established over a battery of files when the corpus was built.
"""

import concurrent.futures as futures
import errno
import hashlib
import json
import os
import re
import shutil
import stat
import struct
import subprocess
import sys
import tempfile
import time
import unicodedata
import zlib
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

CHECK_JOINED = ("No syntax or stream encoding errors found; the file may still "
                "contain errors that qpdf cannot detect")
CHECK_SENTENCE = re.compile(r"\s+".join(re.escape(w) for w in CHECK_JOINED.split(" ")))
CHECK_LEADS = ("checking ", "PDF Version:", "No syntax or stream encoding errors found;")
CHECK_EXACT = ("File is linearized", "File is not linearized")
_DIGITS = re.compile(r"\d+")


def _text(s):
    if isinstance(s, bytes):
        return s.decode("utf-8", "replace")
    return s


def collapse(s):
    return _WS.sub(" ", s).strip()


def normalize_text(s):
    """Comparison form for extracted text: the character sequence, not the layout."""
    if isinstance(s, bytes):
        s = s.decode("utf-8", "replace")
    s = unicodedata.normalize("NFC", s)
    s = s.replace("­", "")
    s = _CTRL.sub(" ", s)
    return _WS.sub(" ", s).strip()


def normalize_table(s):
    rows = []
    for line in _text(s).splitlines():
        line = line.strip()
        if not line or set(line) <= set("- "):
            continue
        rows.append(_WS.sub(" ", line))
    return "\n".join(rows)


def info_fields(s, keys):
    got = {}
    for line in _text(s).splitlines():
        if ":" in line:
            k, v = line.split(":", 1)
            got.setdefault(k.strip(), _WS.sub(" ", v.strip()))
    return "\n".join(f"{k}: {got[k]}" for k in keys if k in got)


def strip_operand_path(s, operand, basename):
    """Make an echoed file operand path-independent.

    The expectations were captured with a bare basename as the operand; this runner
    passes CORPUS_DIR/basename. Without this, every case whose tool echoes its operand
    would fail on the directory prefix alone -- a harness artefact that looks exactly
    like a broken candidate. Applied to both sides, so the comparison is
    path-independent rather than merely one-sided.
    """
    s = _text(s)
    if operand and operand != basename:
        s = s.replace(operand, basename)
    return re.sub(r"\S*/" + re.escape(basename), basename, s)


def normalize_check(s, operand, basename):
    s = CHECK_SENTENCE.sub(CHECK_JOINED, strip_operand_path(s, operand, basename))
    out = []
    for line in s.splitlines():
        c = collapse(line)
        if c.startswith(CHECK_LEADS) or c in CHECK_EXACT:
            out.append(c)
    return "\n".join(out)


def normalize_checklin(s, operand, basename):
    """4.1 check-linearization: the report's own lines, collapsed, in order.

    Two line forms carry the whole verdict -- "FILE: no linearization errors" and
    "FILE is not linearized" -- and an inconsistent file prints NEITHER, writing its
    diagnosis to standard error instead. An empty comparison form is therefore a real
    answer here, not a missing one, which is why nothing is synthesised when stdout
    is blank.
    """
    s = strip_operand_path(s, operand, basename)
    out = []
    for line in s.splitlines():
        c = collapse(line)
        if c.endswith(": no linearization errors") or c.endswith(" is not linearized"):
            out.append(c)
    return "\n".join(out)


def warning_set(out, err, operand, basename):
    """4.1 warnings: the unordered, de-duplicated set of warning identities."""
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


# ---------------------------------------------------------------- PNG decoding

class BadPNG(Exception):
    pass


def _unfilter(raw, w, h):
    stride = w * 3
    out = bytearray()
    prev = bytearray(stride)
    p = 0
    for _ in range(h):
        if p >= len(raw):
            raise BadPNG("truncated image data")
        f = raw[p]
        p += 1
        line = bytearray(raw[p:p + stride])
        if len(line) != stride:
            raise BadPNG("short scanline")
        p += stride
        if f == 0:
            pass
        elif f == 1:
            for x in range(3, stride):
                line[x] = (line[x] + line[x - 3]) & 255
        elif f == 2:
            for x in range(stride):
                line[x] = (line[x] + prev[x]) & 255
        elif f == 3:
            for x in range(stride):
                a = line[x - 3] if x >= 3 else 0
                line[x] = (line[x] + ((a + prev[x]) >> 1)) & 255
        elif f == 4:
            for x in range(stride):
                a = line[x - 3] if x >= 3 else 0
                b = prev[x]
                c = prev[x - 3] if x >= 3 else 0
                pp = a + b - c
                pa, pb, pc = abs(pp - a), abs(pp - b), abs(pp - c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[x] = (line[x] + pr) & 255
        else:
            raise BadPNG("filter type %d" % f)
        out += line
        prev = line
    return bytes(out)


def png_raster(path):
    """Return (w, h, rgb_bytes) for a well-formed 8-bit RGB non-interlaced PNG.

    Raises BadPNG for anything else -- including a palettized or 16-bit or interlaced
    image, which 4.1 names as a structural precondition rather than a content
    difference: such a case fails without being hashed.
    """
    d = read_regular(path)
    if d[:8] != b"\x89PNG\r\n\x1a\n":
        raise BadPNG("not a PNG")
    i = 8
    idat = []
    w = h = None
    seen_ihdr = False
    while i + 8 <= len(d):
        ln = struct.unpack(">I", d[i:i + 4])[0]
        typ = d[i + 4:i + 8]
        data = d[i + 8:i + 8 + ln]
        if len(data) != ln:
            raise BadPNG("truncated chunk")
        i += 12 + ln
        if typ == b"IHDR":
            if ln != 13:
                raise BadPNG("bad IHDR")
            w, h, bd, ct, comp, filt, il = struct.unpack(">IIBBBBB", data)
            if bd != 8:
                raise BadPNG("bit depth %d, want 8" % bd)
            if ct != 2:
                raise BadPNG("colour type %d, want 2 (truecolour RGB)" % ct)
            if il != 0:
                raise BadPNG("interlaced")
            if comp != 0 or filt != 0:
                raise BadPNG("unknown compression/filter method")
            seen_ihdr = True
        elif typ == b"IDAT":
            idat.append(data)
        elif typ == b"IEND":
            break
    if not seen_ihdr:
        raise BadPNG("no IHDR")
    if not idat:
        raise BadPNG("no IDAT")
    if w == 0 or h == 0:
        raise BadPNG("zero dimension")
    try:
        raw = zlib.decompress(b"".join(idat))
    except zlib.error as exc:
        raise BadPNG("bad deflate stream: %s" % exc)
    return w, h, _unfilter(raw, w, h)


def raster_hash(path):
    """4.1's canonical serialization: "RGB8 W H\\n" then the samples, red first."""
    return raster_hash_blank(path)[0]


def raster_hash_blank(path):
    """(raster_hash(path), True when every sample is 0xff, i.e. the page is all white)."""
    w, h, rgb = png_raster(path)
    d = hashlib.sha256()
    d.update(("RGB8 %d %d\n" % (w, h)).encode())
    d.update(rgb)
    return d.hexdigest(), not rgb.strip(b"\xff")


# TEST_INTERFACE.md 4.1: a page whose reference rendering is entirely white earns its case
# only when the same build also matches at least one scored page that is not. Three scored
# pages are blank by construction (their only fill is carried off the page by the CTM), and
# a build that paints nothing at all would otherwise collect them. judge() marks such a pass
# with this detail; main() withdraws it when no non-blank raster page passed.
BLANK_PASS = "blank-page match"
RASTER_KINDS = ("raster", "rasterset")


_SUFFIX = re.compile(r"-(\d+)\.png$")


def rendered_pages(outdir, prefix):
    """The files one render produced, keyed by the page number in the name.

    2.2: the zero padding is not compared, so the numeric suffix is read as an
    integer. -singlefile writes PREFIX.png with no digits at all.
    """
    found = {}
    try:
        names = os.listdir(outdir)
    except OSError:
        return found
    for fn in names:
        full = os.path.join(outdir, fn)
        if not os.path.isfile(full):
            continue
        m = _SUFFIX.search(fn)
        if m and fn.startswith(prefix + "-"):
            found[int(m.group(1))] = full
        elif fn == prefix + ".png":
            found[-1] = full
    return found


# ------------------------------------------------------- Annex F structure

def _find_lin_dict(data):
    head = data[:1024]
    m = re.search(rb"/Linearized", head)
    if not m:
        return None
    start = head.rfind(b"obj", 0, m.start())
    if start < 0:
        return None
    end = data.find(b"endobj", m.start())
    if end < 0:
        return None
    return data[start:end]


def _num(dic, key):
    m = re.search(rb"/" + key + rb"\s+(\d+)", dic)
    return int(m.group(1)) if m else None


def _obj_body(data, num):
    m = re.search(rb"(?m)^" + str(num).encode() + rb"\s+0\s+obj\b", data)
    if not m:
        return None
    end = data.find(b"endobj", m.end())
    return data[m.end():end if end >= 0 else len(data)]


def _first_page_objnum(data, depth=0):
    """The object number of the first page, by walking /Root -> /Pages -> /Kids.

    Deliberately literal: it follows the same path a reader does, so a file that
    reorders its page tree is judged by the tree rather than by object order.
    Returns None when the structure cannot be followed, in which case the caller
    simply does not make this particular check.
    """
    m = re.search(rb"/Root\s+(\d+)\s+0\s+R", data)
    if not m:
        return None
    cat = _obj_body(data, int(m.group(1)))
    if cat is None:
        return None
    pm = re.search(rb"/Pages\s+(\d+)\s+0\s+R", cat)
    if not pm:
        return None
    node = int(pm.group(1))
    for _ in range(64):
        body = _obj_body(data, node)
        if body is None:
            return None
        if re.search(rb"/Type\s*/Page[^s]", body):
            return node
        km = re.search(rb"/Kids\s*\[\s*(\d+)\s+0\s+R", body)
        if not km:
            return None
        node = int(km.group(1))
    return None


def lin_structure_errors(path):
    """Annex F, checked against the file that claims it. Returns a list of faults.

    This is the part of qpdf 11.3.0's verdict that distinguishes a real linearizer
    from a copy, and its agreement with that tool was measured when the corpus was
    built. It is deliberately conservative: it reports a fault only where the file
    contradicts itself, so a correct file written some other way still passes.
    """
    errs = []
    try:
        data = read_regular(path)
    except OSError as exc:
        return ["unreadable: %s" % exc]

    dic = _find_lin_dict(data)
    if dic is None:
        return ["no linearization parameter dictionary in the first 1024 bytes"]
    L = _num(dic, b"L")
    O = _num(dic, b"O")
    E = _num(dic, b"E")
    N = _num(dic, b"N")
    Tv = _num(dic, b"T")
    hm = re.search(rb"/H\s*\[\s*(\d+)\s+(\d+)", dic)
    if L is None or O is None or E is None or N is None or Tv is None or hm is None:
        return ["parameter dictionary is missing one of /L /O /E /N /T /H"]
    H0, H1 = int(hm.group(1)), int(hm.group(2))

    if L != len(data):
        errs.append("/L = %d but the file is %d bytes" % (L, len(data)))

    # /H must name an object header exactly, and its span must be H1.
    if not re.match(rb"\d+\s+\d+\s+obj", data[H0:H0 + 40]):
        errs.append("/H offset %d is not an object header" % H0)

    # Two cross-reference sections, the first near the front, the main one last.
    xrefs = [m.start() for m in re.finditer(rb"(?m)^xref\b", data)]
    if len(xrefs) < 2:
        errs.append("expected two cross-reference sections, found %d" % len(xrefs))
    else:
        first = xrefs[0]
        mm = re.match(rb"xref\s*\r?\n(\d+)\s+(\d+)\s*\r?\n", data[first:first + 64])
        if not mm:
            errs.append("first-page cross-reference table has no subsection header")
        elif int(mm.group(1)) == 0:
            errs.append("first-page table starts at object 0; F.3.4 wants the "
                        "first-page objects in one subsection with no free entries")
        main = xrefs[-1]
        mh = re.match(rb"xref\s*\r?\n(\d+)\s+(\d+)\s*\r?\n", data[main:main + 64])
        if not mh:
            errs.append("main cross-reference table has no subsection header")
        else:
            first_item = main + mh.end()
            j = Tv
            if j < 0 or j > len(data):
                errs.append("/T = %d is outside the file" % Tv)
            else:
                while j < len(data) and data[j:j + 1] in (b" ", b"\r", b"\n"):
                    j += 1
                if j != first_item:
                    errs.append("/T lands at %d; the first main xref entry is at %d"
                                % (j, first_item))
    # /O must name the FIRST page, and /N the page count.  "Some page object"
    # is not enough: a file whose /O names page three is exactly the kind of
    # near-miss this check exists to catch, and it is one a reader following /O
    # to show the first page would get wrong.
    pages = re.findall(rb"/Type\s*/Page[^s]", data)
    if N != len(pages):
        errs.append("/N = %d but the file holds %d page objects" % (N, len(pages)))
    first = _first_page_objnum(data)
    om = re.search(rb"(?m)^" + str(O).encode() + rb"\s+0\s+obj", data)
    if not om:
        errs.append("/O = %d names no object" % O)
    else:
        body = data[om.end():om.end() + 400]
        if b"/Page" not in body:
            errs.append("/O = %d is not a page object" % O)
        elif first is not None and first != O:
            errs.append("/O = %d but the page tree's first page is object %d"
                        % (O, first))
        if not (0 < E <= len(data)):
            errs.append("/E = %d is outside the file" % E)
        elif E <= om.start():
            errs.append("/E = %d is at or before the first page object at %d"
                        % (E, om.start()))
    return errs


def _scratch():
    d = tempfile.mkdtemp(prefix="s8case-", dir="/tmp")
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
        p = subprocess.run(argv, capture_output=True, timeout=timeout, env=env,
                           cwd="/app", stdin=subprocess.DEVNULL)
        return p.returncode, p.stdout, p.stderr
    except subprocess.TimeoutExpired:
        return -9, b"", b"timeout"
    except FileNotFoundError:
        return -127, b"", b"not built"
    except Exception as exc:  # noqa: BLE001 - a broken candidate scores, never crashes us
        return -1, b"", str(exc).encode()


PRODUCT_NAME = "product.pdf"
# The kinds whose RECORDED side is rewritten by strip_operand_path. Every other
# kind compares a normalized actual against the expectation EXACTLY AS STORED,
# which is why the generator stores pre-normalized expectations for all of them.
PATH_KINDS = ("check", "lindump", "warn", "sig", "checklin")
# The raster kinds are NOT path kinds: their recorded side is a hash of decoded
# samples, which carries no file name to rewrite. Only the ACTUAL side is
# transformed (decode, then canonicalize), and the expectation is stored exactly
# as it will be compared.


def _argv_for(spec, path, extra=None):
    argv = [os.path.join(BUILD, spec["tool"])] + list(spec.get("args", []))
    argv.append(path)
    if spec.get("stdout_operand"):
        argv.append("-")
    if extra is not None:
        argv.append(extra)
    return argv


def _actual(kind, spec, out, err, path, base, scratch):
    if kind == "text":
        return normalize_text(out)
    if kind == "info":
        return info_fields(out, spec["keys"])
    if kind == "fonts":
        return normalize_table(out)
    if kind in ("stream", "raw"):
        return hashlib.sha256(out).hexdigest()
    if kind == "check":
        return normalize_check(out, path, base)
    if kind == "checklin":
        return normalize_checklin(out, path, base)
    if kind == "warn":
        return warning_set(out, err, path, base)
    return None


def judge(case, timeout):
    """Returns (passed, detail). Never raises."""
    kind = case["kind"]
    base = case["file"]
    path = os.path.join(CORPUS_DIR, base)
    scratch = None
    try:
        # ---------------------------------------------------- raster kinds
        if kind in ("raster", "rasterset", "absent"):
            scratch = _scratch()
            prefix = "p"
            rc, out, err = run_tool(
                _argv_for(case, path, os.path.join(scratch, prefix)), timeout)
            # 2.3: pdftoppm's exit status is not compared in any scored case.
            got = rendered_pages(scratch, prefix)
            if kind == "absent":
                # The scored property is the ABSENCE of a file. A renderer that
                # writes a blank page instead of declining fails here, which is the
                # whole point of scoring it.
                #
                # But absence only means something if the program RAN. A candidate
                # that never built pdftoppm also produces no file, and would
                # otherwise collect this case for free -- a zero from a check that
                # could not have succeeded is not evidence. So the run itself must
                # have happened: a negative status here is "not built", "timed out"
                # or "could not be executed", none of which is a decision to decline.
                # run_tool prefixes every argv with the verifier's launcher, which
                # EXISTS -- so a candidate that never built the renderer does not
                # raise FileNotFoundError here, it gets the launcher's own 127.
                # Checking for a negative status alone would therefore pass the very
                # case this check exists to catch. Assert the thing that must have
                # run was actually there.
                tool = os.path.join(BUILD, case["tool"])
                if not os.access(tool, os.X_OK):
                    return False, ("%s is not executable; an absence produced by a "
                                   "missing renderer proves nothing" % case["tool"])
                if rc < 0 or rc == 127:
                    return False, ("tool did not run (rc=%d); absence proves nothing"
                                   % rc)
                if got:
                    return False, "expected no file, got %d" % len(got)
                return True, ""
            want_pages = case.get("pages", 1)
            if len(got) != want_pages:
                # The count is checked FIRST and each missing page is a failure in
                # its own right: a file that is not there cannot differ from
                # anything, so comparing only what exists would reward skipping.
                return False, "produced %d files, expected %d" % (len(got), want_pages)
            keys = sorted(got)
            hashes = []
            blank = True
            for k in keys:
                try:
                    hsh, white = raster_hash_blank(got[k])
                except BadPNG as exc:
                    return False, "structural: %s" % exc
                except Exception as exc:  # noqa: BLE001
                    return False, "undecodable: %s" % exc
                hashes.append(hsh)
                blank = blank and white
            if "\n".join(hashes) == case["expect"]:
                return True, (BLANK_PASS if blank else "")
            return False, "raster mismatch over %d page(s)" % len(hashes)

        # ------------------------------------------------- linearized output
        if kind == "linearize":
            scratch = _scratch()
            prod = os.path.join(scratch, PRODUCT_NAME)
            rc, out, err = run_tool(_argv_for(case, path, prod), timeout)
            exp = case.get("expect_exit")
            if exp is not None and rc != exp:
                return False, f"producer exit {rc} != {exp}"
            if not os.path.exists(prod):
                return False, "producer wrote no file"
            errs = lin_structure_errors(prod)
            if errs:
                return False, "Annex F: " + "; ".join(errs[:3])
            then = case.get("then")
            if then:
                rc2, out2, err2 = run_tool(_argv_for(then, prod), timeout)
                exp2 = then.get("expect_exit")
                if exp2 is not None and rc2 != exp2:
                    return False, f"consumer exit {rc2} != {exp2}"
                got = _actual(then["kind"], then, out2, err2, prod, PRODUCT_NAME,
                              scratch)
                if got is None:
                    return False, "unknown then-kind %s" % then["kind"]
                if got != case["expect"]:
                    return False, ("product/%s mismatch: got %r want %r"
                                   % (then["kind"], got[:70], case["expect"][:70]))
            return True, ""

        # ------------------------------------------------ single-run kinds
        extra = None
        if case.get("out_operand"):
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
    blank_passes = []      # cases credited only on a blank page (see BLANK_PASS)
    painted_pass = False   # some raster-kind case matched a page that is not blank

    def work(idx_case):
        idx, case = idx_case
        try:
            ok, detail = judge(case, per_case_timeout)
        except Exception as exc:  # noqa: BLE001
            ok, detail = False, f"runner error: {exc}"
        return idx, case, ok, detail

    with futures.ThreadPoolExecutor(max_workers=workers) as pool:
        futs = {pool.submit(work, ic): ic[0] for ic in enumerate(cases)}
        for fut in futures.as_completed(futs):
            idx, case, ok, detail = fut.result()
            done.add(idx)
            g = case["group"]
            passed, total = results.get(g, (0, 0))
            results[g] = (passed + (1 if ok else 0), total + 1)
            ran[g] = ran.get(g, 0) + 1
            if not ok:
                detail_lines.append(f"FAIL {g} {case['id']}: {detail}")
            elif detail == BLANK_PASS:
                blank_passes.append(case)
            elif case["kind"] in RASTER_KINDS:
                painted_pass = True
            if time.time() - start > deadline:
                for f2 in futs:
                    f2.cancel()
                break

    for idx, case in enumerate(cases):
        if idx not in done:
            g = case["group"]
            passed, total = results.get(g, (0, 0))
            results[g] = (passed, total + 1)
            detail_lines.append(f"UNRUN {g} {case['id']}: deadline")

    if blank_passes and not painted_pass:
        for case in blank_passes:
            g = case["group"]
            passed, total = results[g]
            results[g] = (passed - 1, total)
            detail_lines.append(
                f"FAIL {g} {case['id']}: blank page matched, but no non-blank "
                "page did; a blank page alone is not evidence of rendering "
                "(TEST_INTERFACE.md 4.1)")

    os.makedirs(log_dir, exist_ok=True)
    with open(os.path.join(log_dir, "groups.txt"), "a") as fh:
        for g in sorted(results):
            p, t = results[g]
            fh.write(f"{g} {p} {t}\n")
    with open(os.path.join(log_dir, "ran.txt"), "a") as fh:
        for g in sorted(results):
            p, t = results[g]
            fh.write(f"{g} ran={ran.get(g, 0)} counted={t} passed={p}\n")
    with open(os.path.join(log_dir, "failures.txt"), "a") as fh:
        fh.write("\n".join(detail_lines[:4000]) + "\n")
    for g in sorted(results):
        p, t = results[g]
        print(f"{g}: {p}/{t} (ran {ran.get(g, 0)})")
    print(f"elapsed {time.time() - start:.1f}s over {len(cases)} cases", flush=True)


if __name__ == "__main__":
    main()
