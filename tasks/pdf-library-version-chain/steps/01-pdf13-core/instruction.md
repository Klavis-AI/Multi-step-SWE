# A PDF reader for PDF 1.0 through 1.3

## 1. Implement this milestone

Build a PDF implementation in this workspace, from nothing: an object model and lexer,
cross-reference handling with recovery, the standard stream filters, the page tree, a
content-stream interpreter, font handling and text extraction. It is written in **C**
and built by `make`.

Four command-line utilities expose it, each an existing published interface adopted
unchanged — `pdfinfo`, `pdftotext` and `pdffonts` as the poppler-utils man pages define
them, and `qpdf --show-object` as the qpdf manual defines it. `/app/TEST_INTERFACE.md`
fixes the build command, the exact option subset, the one environment hook, and the
output each utility must reproduce.

Those four utilities are the only way any of this is reached. `make`, run in `/app`,
must put them at **`/app/build/pdfinfo`, `/app/build/pdftotext`, `/app/build/pdffonts`
and `/app/build/qpdf`** -- that is where they are invoked from, so a library that works
but is not reachable at those four paths cannot be used at all. Build the four
entry points first and keep them building: even a complete milestone of correct
parsing is unusable behind a missing `Makefile`, or behind binaries left somewhere
other than `/app/build/`.

What must work is the file format itself, as `/app/SPEC/pdf13.md` sets it out
against ISO 32000-1:2008:

* the object grammar and lexical conventions, including literal strings with nested
  parentheses and octal escapes, hexadecimal strings, `#xx` escapes in names, and
  streams whose `/Length` is an indirect reference;
* the cross-reference table, its subsections and free list, the trailer, and a header
  that is not at byte offset 0;
* **recovery** — a file whose cross-reference information is missing, truncated,
  corrupt, or simply wrong must still be read, by scanning for what is actually there;
* `ASCIIHexDecode`, `ASCII85Decode`, `LZWDecode` with `/EarlyChange` 0 and 1,
  `FlateDecode` and `RunLengthDecode`, chained in any order, with the TIFF and PNG
  predictors; a stream whose chain cannot be fully decoded still has its length
  resolved and its raw bytes returned;
* the document catalog and page tree, including intermediate nodes, the inheritable
  attributes, and a `/Count` that disagrees with the tree;
* the content-stream interpreter, the graphics and text state, all text-showing
  operators and their displacement rules, Form XObjects, and inline images whose
  binary data must not be read as operators;
* simple fonts, Type 3 fonts, the standard 14 and their metrics, the standard
  encodings and `/Differences`, composite fonts with a CMap, and `/ToUnicode`;
* text extraction, and the document information dictionary.

A file that declares a later version in its header but uses nothing beyond the above
must be read, not refused.

## 2. Read these local references

**Normative semantics:** `/opt/pdfspec/ISO-32000-1-2008.txt` — the published
ISO 32000-1:2008 text, the definition of the file format to implement.
`/app/SPEC/pdf13.md` — the clauses this milestone covers, in that document's terms.

**A standard these clauses rely on**, copied into the image: `/opt/docs/agl/glyphlist.txt` — the Adobe Glyph List 2.0, which ISO 32000-1 9.10.2 consults to map a glyph name to Unicode (`aglfn.txt` beside it is its list for new fonts).

**The interfaces' own documentation**, copied into the image:
`/opt/pdfspec/man-pdfinfo.txt`, `/opt/pdfspec/man-pdftotext.txt`,
`/opt/pdfspec/man-pdffonts.txt` (the poppler-utils man pages) and
`/opt/pdfspec/qpdf-running-qpdf.txt` (the qpdf manual's command-line chapter).

**Additional files:** `/app/TEST_INTERFACE.md` — the build command, the utilities'
option spellings, the `SOURCE_DATE_EPOCH` hook, and exactly how each output must match
the reference output. `/opt/pdf-tests-public/` — a sample corpus
with expected answers taken from the same reference tools, run with
`bash /opt/pdf-tests-public/run-public-tests.sh`; use it to check progress. It covers
only part of the milestone, so passing it is not the whole milestone.
`/app/WORKSPACE_HANDOFF.md` — leave a reproducible handoff there.

## 3. Meta requirements

- The implementation budget is 7,200 seconds. Check `bash /app/timer.sh`; the configured timeout is authoritative and resets for this step.
- Work in the cumulative `/app` workspace and preserve the supplied documents and
  handoff files. Do not create a prose answer or an alternate entry point unless
  Section 1 requires one.
- The utilities must build from source with `make` in a clean copy of `/app`, so
  leave it source-only: delete build products outside the `Makefile`'s own
  output directory before you finish, and compile scratch programs under `/tmp`. The
  workspace must also survive being archived, moved and unpacked, so keep the
  files you write to plain ASCII; put any
  non-ASCII byte a test needs inside a string literal with an escape sequence rather
  than as a raw byte in the source.
- Do not cheat by using online solutions or hints specific to this task.
- The built binaries must carry no third-party PDF engine markers. The
  strings `MuPDF`, `mupdf`, `Artifex`, `Ghostscript`, `poppler`, `pdfium`,
  `PDFium`, `libqpdf`, `qpdf version`, `xpdf`, `Xpdf`, `HarfBuzz`, and
  `FreeType` must not appear anywhere in any binary under `/app/build/` —
  including in `--help` or `--version` text copied from a man page. No
  symbol `readelf` lists in them may carry a third-party prefix (`fz_`,
  `FPDF`, `opj_`, `jbig2_`, `hb_`), whether it is exported or file-local. None of these names
  arises in a library written from scratch; the adopted CLI names (`pdfinfo`,
  `pdftotext`, etc.) are fine.
- Work only from this workspace and the local references named above; do not
  search the system for other implementations or answers.
- Run the local checks named above and keep `/app/WORKSPACE_HANDOFF.md` an accurate
  description of the repository.
