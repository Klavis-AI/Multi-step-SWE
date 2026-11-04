#!/usr/bin/env python3
"""Comparison logic for the self-check.

Uses the same normalization as the milestone's comparisons: extracted text is compared as
a character sequence (NFC, control characters dropped, whitespace runs collapsed), not
as a layout, and the pdffonts table is compared row by row with runs of spaces
collapsed. The utilities are invoked exactly as their man pages define them.

One comparison form per case kind, following TEST_INTERFACE.md 4.1:

  exit        run any of the below, compare only the exit status
  info        `pdfinfo ARGS FILE`, the compared field subset of stdout
  text        `pdftotext ARGS FILE -`, compared after normalization
  fonts       `pdffonts ARGS FILE`, the normalized table
  stream      `qpdf --show-object=N[,G] --filtered-stream-data FILE`, sha256
  raw         `qpdf --show-object=N[,G] --raw-stream-data FILE`, sha256
  pages       `qpdf --show-pages`, compared as a normalized line sequence
  xref        `qpdf --show-xref`, compared as a normalized line sequence
  object      `qpdf --show-object=...`, whole dump with whitespace runs collapsed
  check       `qpdf --check`, only the four compared line forms, wrapped lines joined
  linparams   `qpdf --show-linearization`, the parameter block alone
  lindump     `qpdf --show-linearization`, the full dump minus per-page hint entries
  warn        the set of warning identities over stdout and stderr merged
  encryption  `qpdf --show-encryption`, or the same block printed inline by `--check`
  imagelist   `pdfimages -list`, the table minus its header line and dashed rule
  codestream  `pdfimages -jbig2 FILE ROOT`, the file names produced plus each sha256
  sig         `pdfsig`, only the compared line forms of the signature report
  product     one of the writing options, then a second utility over the product

Two operand shapes go beyond a plain FILE. `out_operand` appends a scratch output path
after the file (the writing options, and `pdfimages`' IMAGE-ROOT); `product` runs the
writing option first, compares its exit status, and then judges a second utility run
over the file it wrote, by that utility's own rule. Neither the product nor the
extracted image files are compared byte for byte: 4.1 makes object numbering in a
product free, and removes decoded pixels from the comparison entirely.

The file operand
----------------
Every expectation here was recorded by running the reference tool with the corpus
file's BARE BASENAME as the operand, and qpdf echoes that operand verbatim -- into
`checking FILE`, `FILE is not linearized`, `FILE: linearization data:` and each
`WARNING: FILE: ...` prefix. This driver must pass a path, so the echoed operand is
made path-independent before comparison; see strip_operand_path().
"""
import hashlib, json, os, re, shutil, subprocess, tempfile, unicodedata

HERE = os.path.dirname(os.path.abspath(__file__))
CORPUS = os.environ.get("CORPUS_DIR", os.path.join(HERE, "corpus"))
BUILD = os.environ.get("CAND_BUILD", "/app/build")

_WS = re.compile(r"\s+")
_CTRL = re.compile(r"[\x00-\x08\x0b\x0c\x0e-\x1f\x7f]")
_DIGITS = re.compile(r"\d+")

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

# 4.1 encryption: the block qpdf prints for --show-encryption and inline in --check.
# The three `encryption method:` lines are emitted only when /V >= 4, and their
# ABSENCE is as much a part of the answer as their content, so nothing is synthesised
# here -- the filter keeps what the tool printed, in its order.
ENC_LEADS = ("Incorrect password supplied",
             "R = ", "P = ", "User password =",
             "Supplied password is",
             "extract for accessibility:", "extract for any purpose:",
             "print low resolution:", "print high resolution:",
             "modify document assembly:", "modify forms:", "modify annotations:",
             "modify other:", "modify anything:",
             "stream encryption method:", "string encryption method:",
             "file encryption method:")
# 4.1 signature report: ONLY these line forms are compared. Signature Validation, Not
# total document signed, the signer certificate fields, Signing Hash Algorithm and
# Signing Time are excluded -- the image holds no certificate database, so validation
# fails identically for every signature and no case asks whether one is valid.
SIG_LEADS = ("Digital Signature Info of:", "Signature #",
             "- Signature Field Name:", "- Signature Type:", "- Signed Ranges:",
             "The signature form field is not signed.")
SIG_NOSIG = re.compile(r"^File '.*' does not contain any signatures$")
# 4.1 imagelist: the column header line and the dashed rule beneath it are ignored.
IMAGELIST_HDR = ("page", "num", "type")

# The basename every writing option's product is given, in the recording and here
# alike, so that a `product` case's second stage is path-independent for the same
# reason every other operand is: the name on both sides is the same name.
PRODUCT_NAME = "product.pdf"
# The kinds whose RECORDED side is rewritten by strip_operand_path. Every other
# kind compares a normalized actual against the expectation EXACTLY AS STORED,
# which is why the generator stores pre-normalized expectations for all of them.
PATH_KINDS = ("check", "lindump", "warn", "sig")


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

    The expectations were captured with a bare basename as the operand; this driver
    passes CORPUS/basename. Without this, every check / lindump / warn / sig case
    would fail on the directory prefix alone -- a harness artefact that looks exactly
    like a broken implementation.

    Two rewrites, both confined to the directory prefix sitting directly in front of
    an occurrence of this case's own basename:

      1. the operand exactly as this driver spelled it, wherever it appears;
      2. any remaining occurrence of the basename preceded by a path prefix -- a run
         of non-whitespace ending in "/".

    (1) alone would only work for a tool that echoes our argv verbatim. (2) is what
    makes the rule hold when the utility spells the operand some other way -- "./NAME",
    a ".."-relative path, a resolved symlink, a path this driver never constructed.

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
    """4.1 check: only the four compared line forms, wrapped lines joined first."""
    s = CHECK_SENTENCE.sub(CHECK_JOINED, strip_operand_path(s, operand, basename))
    out = []
    for line in s.splitlines():
        c = collapse(line)
        if c.startswith(CHECK_LEADS) or c in CHECK_EXACT:
            out.append(c)
    return "\n".join(out)


def normalize_lin_params(s):
    """4.1 linearization dump, parameter block alone: the eight compared keys."""
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
    reproduces the reference tool's values for them, so they cannot be compared; the
    expectations here were recorded with this same rule applied.
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
    the comparison form is the empty string -- which 4.1 names as a real answer.
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
            with open(p, "rb") as fh:
                rows.append("%s %s" % (fn, hashlib.sha256(fh.read()).hexdigest()))
        except OSError:
            rows.append("%s <unreadable>" % fn)
    return "\n".join(rows)


def load_cases():
    with open(os.path.join(HERE, "cases.json")) as fh:
        return json.load(fh)


class _ToolFailure(Exception):
    """A run that produced no answer at all, as opposed to a wrong one."""


def _run(argv, timeout):
    # Drive the utilities exactly as TEST_INTERFACE.md specifies: same working
    # directory, same restricted PATH, same locale and frozen clock. A harness that ran
    # them with the shell's own environment could pass here and still fail under that
    # environment, which is the worst kind of progress signal.
    env = {
        "PATH": BUILD,
        "LC_ALL": "C.UTF-8",
        "HOME": "/tmp",
        "TMPDIR": "/tmp",
        "SOURCE_DATE_EPOCH": "1700000000",
    }
    try:
        p = subprocess.run(argv, capture_output=True, timeout=timeout, env=env,
                           cwd="/app", stdin=subprocess.DEVNULL)
        return p.returncode, p.stdout, p.stderr
    except subprocess.TimeoutExpired:
        raise _ToolFailure("timeout")
    except FileNotFoundError:
        raise _ToolFailure("%s not built" % os.path.basename(argv[0]))


def _argv_for(spec, path, extra=None):
    """The documented operand order: options, then FILE, then the one trailing
    operand the interface defines for this command (`-` for standard output, an
    OUTFILE for the writing options, an IMAGE-ROOT for pdfimages)."""
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


def judge(case, timeout=30):
    kind = case["kind"]
    base = case["file"]
    path = os.path.join(CORPUS, base)
    scratch = None
    try:
        # ---- product: a writing option, then a utility over what it wrote ----
        # 4.1: the product is never compared byte for byte -- object numbering and
        # order are free. What is compared is the producing run's exit status and
        # then the answer the second utility gives, by that utility's own rule.
        if kind == "product":
            scratch = tempfile.mkdtemp(prefix="public-case-")
            prod = os.path.join(scratch, PRODUCT_NAME)
            rc, out, err = _run(_argv_for(case, path, prod), timeout)
            exp = case.get("expect_exit")
            if exp is not None and rc != exp:
                return False, "producer exit %d != %d" % (rc, exp)
            if not os.path.exists(prod):
                return False, "producer wrote no file"
            then = case["then"]
            tkind = then["kind"]
            rc2, out2, err2 = _run(_argv_for(then, prod), timeout)
            exp2 = then.get("expect_exit")
            if exp2 is not None and rc2 != exp2:
                return False, "consumer exit %d != %d" % (rc2, exp2)
            if tkind == "exit":
                return True, ""
            got = _actual(tkind, then, out2, err2, prod, PRODUCT_NAME, scratch)
            if got is None:
                return False, f"unknown then-kind {tkind}"
            want = case["expect"]
            if tkind in PATH_KINDS:
                want = strip_operand_path(want, prod, PRODUCT_NAME)
            if got == want:
                return True, ""
            return False, f"got {got[:100]!r} want {want[:100]!r}"

        # ---- single-run kinds, with the one trailing operand some of them take --
        extra = None
        if kind == "codestream":
            scratch = tempfile.mkdtemp(prefix="public-case-")
            extra = os.path.join(scratch, "img")
        elif case.get("out_operand"):
            scratch = tempfile.mkdtemp(prefix="public-case-")
            extra = os.path.join(scratch, PRODUCT_NAME)

        rc, out, err = _run(_argv_for(case, path, extra), timeout)

        exp_exit = case.get("expect_exit")
        if exp_exit is not None and rc != exp_exit:
            return False, f"exit {rc} != {exp_exit}"
        if kind == "exit":
            return True, ""

        # A matching exit status says nothing about what is on stdout: one fixture
        # exits 3 with a full dump, another exits 3 with stdout empty, and malformed
        # input exits 2 with stdout empty. Content is therefore always compared from
        # the output itself, never inferred from rc, and an empty stdout is a real
        # answer that fails unless the recorded expectation is empty too.
        got = _actual(kind, case, out, err, path, base, scratch)
        if got is None:
            return False, f"unknown kind {kind}"

        want = case["expect"]
        if kind in PATH_KINDS:
            # The same rewrite on the recorded side, so the comparison is
            # path-independent in both directions. It is the identity on every
            # expectation recorded with a bare basename, which is all of them.
            want = strip_operand_path(want, path, base)

        if got == want:
            return True, ""
        return False, f"got {got[:100]!r} want {want[:100]!r}"
    except _ToolFailure as exc:
        return False, str(exc)
    finally:
        if scratch:
            shutil.rmtree(scratch, ignore_errors=True)
