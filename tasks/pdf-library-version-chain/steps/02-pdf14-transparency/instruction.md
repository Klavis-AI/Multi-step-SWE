# PDF 1.4: transparency structure, incremental updates and linearized files

## 1. Implement this milestone

Extend the implementation already in this workspace to PDF 1.4: transparency
group and soft-mask structure, the blend-mode and alpha parameters, incremental
updates, and reading linearized files. It is written in **C** and built by
`make`, and everything that read correctly before must still read correctly now.

The same four command-line utilities expose it, each still the published
interface it already was, but this milestone reaches more of the qpdf one:
`--check`, `--show-xref`, `--show-pages` and `--show-linearization` join
`--show-object`, which now also takes `trailer`. `/app/TEST_INTERFACE.md` fixes
the build command, the option subset, the one environment hook, and the output each utility must reproduce.

Those four utilities are still the only way any of this is reached. `make`, run
in `/app`, must put them at **`/app/build/pdfinfo`, `/app/build/pdftotext`,
`/app/build/pdffonts` and `/app/build/qpdf`** -- that is where they are invoked
from, so a library that works but is not reachable at those four paths cannot be
used at all. Build the four entry points first and keep them building: even a
complete milestone of correct parsing is unusable behind a missing `Makefile`, or
behind binaries left somewhere other than `/app/build/`.

What must work is what `/app/SPEC/pdf14.md` sets out against ISO 32000-1:2008:

* group attributes dictionaries — a page's `/Group` and a form XObject's
  `/Group`, with `/S`, `/CS`, `/I` and `/K` — reported exactly as the file gives
  them, a subtype or colour space that means nothing included;
* `/SMask` in the graphics state: a dictionary with `/S`, `/G`, `/BC` and `/TR`,
  or the name `/None`, distinct from both a mask and an absent key; a `/G` that
  resolves to nothing is not an error;
* `/BM` — a name or an array of names — with `/CA`, `/ca`, `/AIS` and `/TK`, all
  preserved as written, a number's spelling and an unrecognized mode name alike;
* **incremental updates**: update sections each with their own cross-reference
  section, trailer and `%%EOF`, chained by `/Prev`, the most recent definition of
  every object winning at any depth. An update may change a page's content, the
  document information dictionary, a `/MediaBox`, the page tree — adding or
  deleting pages — or the trailer's `/Root`, and each must show in what the
  utilities report;
* a `/Prev` pointing at no cross-reference section must not lose the document:
  recover it as the previous milestone already required, and report the damage;
* **linearized files**: the linearization parameter dictionary, the first-page
  cross-reference section and the hint tables, read and dumped. `/L` must equal
  the real file length before any of the rest is believed; a file whose `/L` is
  wrong is an ordinary file that happens to contain a dictionary.

Computing the composite result of a group, a mask or a blend mode is not
required; what is required is the structure in the file and the behaviour of a
reader that resolves updates. A later-version header over such a body must be read.

## 2. Read these local references

**Normative semantics:** `/opt/pdfspec/ISO-32000-1-2008.txt` — the published
ISO 32000-1:2008 text, the definition of the file format to implement.
`/app/SPEC/pdf14.md` — the clauses this milestone covers, in that document's
terms; `/app/SPEC/pdf13.md` — the previous milestone's, all of which still apply.

**A standard these clauses rely on**, copied into the image: `/opt/docs/agl/glyphlist.txt` — the Adobe Glyph List 2.0, which ISO 32000-1 9.10.2 consults to map a glyph name to Unicode (`aglfn.txt` beside it is its list for new fonts).

**The interfaces' own documentation**, copied into the image: `/opt/pdfspec/man-pdfinfo.txt`, `/opt/pdfspec/man-pdftotext.txt`,
`/opt/pdfspec/man-pdffonts.txt` (the poppler-utils man pages) and
`/opt/pdfspec/qpdf-running-qpdf.txt` (the qpdf manual's command-line chapter,
which defines every qpdf option used here, the four new ones included, and
qpdf's exit status).

**Additional files:** `/app/TEST_INTERFACE.md` — the build command, the option
spellings, the `SOURCE_DATE_EPOCH` hook, and how each output must match the
reference output. `/app/tests-public/` — **this milestone's** sample
corpus with expected answers from the same reference tools, run with
`bash /app/tests-public/run-public-tests.sh`; it covers only part of the
milestone, so passing it is not the whole milestone. `/opt/pdf-tests-public/` holds the first milestone's
sample and still runs, as a regression check on earlier behaviour. `/app/WORKSPACE_HANDOFF.md` — keep it accurate.

## 3. Meta requirements

- The implementation budget is 7,200 seconds. Check `bash /app/timer.sh`;
  the configured timeout is authoritative and resets for this step.
- Work in the cumulative `/app` workspace and preserve the supplied documents and
  handoff files. Do not create a prose answer or an alternate entry point.
- Nothing that worked before may stop working: files within the previous
  milestone's scope must still read correctly.
- The utilities must build from source with `make` in a clean copy of `/app`,
  so leave it source-only: delete build products outside the
  `Makefile`'s own output directory, and compile scratch programs under `/tmp`.
  It must also survive being archived and unpacked, so keep the
  files you write to plain ASCII, non-ASCII bytes escaped inside string literals.
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
- Run the local checks named above and keep `/app/WORKSPACE_HANDOFF.md` an
  accurate description of the repository.
