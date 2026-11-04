# PDF 1.5: object and cross-reference streams, optional content, JPX passthrough

## 1. Implement this milestone

Extend the implementation already in this workspace to PDF 1.5: cross-reference
streams, object streams, optional content, and the `JPXDecode` filter carried
through unchanged. It is written in **C** and built by `make`, and everything that
read correctly before must still read correctly now.

The same four command-line utilities expose it, each still the published interface
it already was, and this milestone adds **no new option**: the option set of the
previous milestone is the set that reaches this one. `/app/TEST_INTERFACE.md` fixes
the build command, the option subset, the one environment hook, and the output each utility must reproduce.

Those four utilities are still the only way any of this is reached. `make`, run in
`/app`, must put them at **`/app/build/pdfinfo`, `/app/build/pdftotext`,
`/app/build/pdffonts` and `/app/build/qpdf`** -- that is where they are invoked
from, so a library that works but is not reachable at those four paths cannot be
used at all. Build the four entry points first and keep them building: even a
complete milestone of correct parsing is unusable behind a missing `Makefile`, or
behind binaries left somewhere other than `/app/build/`.

What must work is what `/app/SPEC/pdf15.md` sets out against ISO 32000-1:2008:

* **cross-reference streams** — a stream object of `/Type /XRef` carrying, in its
  dictionary what a trailer carried before, and in its data the cross-reference
  entries themselves: `/W` field widths in any combination the standard permits, a
  zero width taking its field's default, `/Index` subsections, `/Size`, and the
  stream's own filter and predictor. `startxref` addresses the stream object, and
  the `xref` and `trailer` keywords need not appear at all. `/Prev` chains between
  cross-reference streams, and in both mixed directions, with the previous
  milestone's rule unchanged: the most recent definition of every object wins;
* **object streams** — a stream of `/Type /ObjStm` holding a sequence of indirect
  objects, located through `/N`, `/First` and the pair header in the decoded data.
  A type-2 cross-reference entry names the object stream and the index within it,
  and **that entry decides which definition of an object is in force**: an object
  that exists both plainly and inside an object stream resolves to whichever the
  cross-reference data selects, not to whichever scanning finds first. A document
  whose page-tree root is compressed cannot be read by scanning for `/Type /Pages`
  at all, because those bytes do not occur in the file;
* **optional content** — `/OCProperties` in the catalogue, optional content groups,
  membership dictionaries, and the default configuration's `/BaseState`, `/ON` and
  `/OFF`. Content made optional by a `/OC` marked-content section, nested or not, or
  by an XObject's `/OC` entry, is included in extracted text when its group is on and
  left out when it is off;
* **JPX passthrough** — a stream whose filter chain contains `JPXDecode` keeps its
  bytes. The filter is not applied here: the raw bytes come back exactly as stored, a
  request to decode such a stream is refused rather than half-answered, and a chain
  pairing `JPXDecode` with a filter you do implement is refused as a whole.

A file declaring 1.3 or 1.4 in its header while using every structure above must
still be read: the header is not a gate on which structures may appear, and
real-world files do exactly this. Painting a page is not required; what is
required is the content a reader includes and the bytes a stream yields.

## 2. Read these local references

**Normative semantics:** `/opt/pdfspec/ISO-32000-1-2008.txt` — the published ISO
32000-1:2008 text, the definition of the file format to implement.
`/app/SPEC/pdf15.md` — the clauses this milestone covers, in that document's terms;
`/app/SPEC/pdf14.md` and `/app/SPEC/pdf13.md` — the previous milestones', all of
which still apply.

**A standard these clauses rely on**, copied into the image: `/opt/docs/agl/glyphlist.txt` — the Adobe Glyph List 2.0, which ISO 32000-1 9.10.2 consults to map a glyph name to Unicode (`aglfn.txt` beside it is its list for new fonts).

**The interfaces' own documentation**, copied into the image: `/opt/pdfspec/man-pdfinfo.txt`, `/opt/pdfspec/man-pdftotext.txt`,
`/opt/pdfspec/man-pdffonts.txt` (the poppler-utils man pages) and
`/opt/pdfspec/qpdf-running-qpdf.txt` (the qpdf manual's command-line chapter, which
defines every qpdf option used here and qpdf's exit status).

**Additional files:** `/app/TEST_INTERFACE.md` — the build command, the option
spellings, the `SOURCE_DATE_EPOCH` hook, and how each output must match the reference
output. `/app/tests-public/` — **this milestone's** sample corpus with
expected answers from the same reference tools, run with
`bash /app/tests-public/run-public-tests.sh`; it covers only part of the milestone,
so passing it is not the whole milestone. `/opt/pdf-tests-public/` holds the first milestone's sample and
still runs, as a regression check on earlier behaviour. `/app/WORKSPACE_HANDOFF.md` — keep it accurate.

## 3. Meta requirements

- The implementation budget is 7,200 seconds. Check `bash /app/timer.sh`; the configured timeout is authoritative and resets for this step.
- Work in the cumulative `/app` workspace and preserve the supplied documents and
  handoff files. Do not create a prose answer or an alternate entry point.
- Nothing that worked before may stop working: files within the previous
  milestones' scope must still read correctly.
- The utilities must build from source with `make` in a clean copy of `/app`,
  so leave it source-only: delete build products outside the `Makefile`'s
  own output directory, and compile scratch programs under `/tmp`. It must also
  survive being archived and unpacked, so keep the files you write to
  plain ASCII, non-ASCII bytes escaped inside string literals.
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
