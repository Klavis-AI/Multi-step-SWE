# PDF/A-1 and PDF/A-2: conformance validation

## 1. Implement this milestone

Extend the implementation already in this workspace to the PDF/A archiving profiles
defined by ISO 19005-1 and ISO 19005-2: **report** whether a file conforms at a requested conformance level. It is written in **C** and built by `make`, and everything that read correctly
before must still read correctly now.

This milestone adds one program to the set the previous milestones built, each
again an existing published command-line interface adopted as-is rather than an
invention of this task: `verapdf`, the veraPDF command-line validator. `/app/TEST_INTERFACE.md` fixes
the build command, the exact option subset of each, the environment hooks, and the
output each utility must reproduce.

Those programs are the only way any of this is reached. `make`, run in `/app`, must
put **every** utility into `/app/build/` under its real program name: the ones this
workspace already builds — **`/app/build/pdfinfo`, `/app/build/pdftotext`,
`/app/build/pdffonts` and `/app/build/qpdf`**, together with any the intervening
milestones added, each keeping the path it already had — and this milestone's
**`/app/build/verapdf`**. That is where each of them is
invoked from, so a library that works but is not reachable at those paths cannot be
used at all. Build the entry points first and keep them building: even a complete
milestone of correct conformance logic is unusable behind a missing `Makefile`, or
behind binaries left somewhere other than `/app/build/`.

What must work is what `/app/SPEC/pdfa1-2.md` sets out against ISO 19005-1:2005 and
ISO 19005-2:2011:

* **validation** — for a file and a requested conformance level, decide conformance
  and say so. The levels are `1b` and `1a` for ISO 19005-1 and `2b`, `2a` and `2u`
  for ISO 19005-2. The requirements that carry this milestone are the ones a real
  archive rejects files for: every font used to show text shall be embedded, with a
  legal subset name and metrics matching the embedded program; colour shall be
  device-independent, or made so by an `/OutputIntent` whose destination profile is
  embedded; `/JavaScript`, `/Launch`, `/Movie`, `/Sound`, `/ResetForm` and
  `/ImportData` actions shall not appear, and neither shall a trigger that would run
  one; and the catalogue shall carry an XMP metadata stream identifying the part and
  conformance level and, under ISO 19005-1, agreeing with the document information
  dictionary.
  `/app/SPEC/pdfa1-2.md` gives these clause by clause, with the part-2 allowances as
  its own section. **`/app/TEST_INTERFACE.md` section 4.2 names the exact clauses and
  test numbers that are required** — a clause outside that list need not be
  implemented. The encryption prohibition is one such:
  it is described here and in the SPEC because it is part of PDF/A, but no verdict
  this milestone requires turns on it;

## 2. Read these local references

**Normative semantics:** `/app/SPEC/pdfa1-2.md` — the clauses of ISO 19005-1:2005 and
ISO 19005-2:2011 this milestone covers, in those documents' terms, and the
authoritative statement of them for this workspace.
`/opt/pdfspec/ISO-32000-1-2008.txt` — the published ISO 32000-1:2008 text, the
underlying file format, which both parts of ISO 19005 restrict rather than replace.
`/app/SPEC/pdf13.md`, `pdf14.md`, `pdf15.md`, `pdf16-17.md` and `pdf20.md` — the
previous milestones' clauses, all of which still apply.

**Standards these clauses rely on**, copied into the image (each PDF original sits beside its `.txt`): `/opt/docs/agl/glyphlist.txt` — the Adobe Glyph List 2.0, which ISO 32000-1 9.10.2 consults to map a glyph name to Unicode (`aglfn.txt` beside it is its list for new fonts); `/opt/docs/fips/NIST.FIPS.197.txt` — FIPS PUB 197 (2001), the AES that ISO 32000-1 7.6.2 names; `/opt/docs/rfc/rfc2898.txt` — RFC 2898 (PKCS #5), the padding `pdf16-17.md` cites; `/opt/docs/fips/NIST.FIPS.180-4.txt` — FIPS PUB 180-4, the SHA-256, SHA-384 and SHA-512 that `pdf20.md`'s Algorithm 2.B uses; `/opt/docs/xmp/XMPSpecificationPart1.txt` and `XMPSpecificationPart2.txt` — Adobe's XMP specification, the data model and packet of part 1 and the standard schemas and their properties of part 2 (the 2012 edition, which revises the September 2005 one ISO 32000-1 cites); `/opt/docs/icc/ICC1V42.txt` — ICC.1:2004-10, the ICC profile format ISO 32000-1 cites.

**The interfaces' own documentation**, copied into the image: `/app/SPEC/verapdf-cli.txt`, the veraPDF command-line usage, defining
`--flavour`, `--format` and the exit status. The poppler-utils man pages and the qpdf chapter still apply.

**Additional files:** `/app/TEST_INTERFACE.md` — the build command, the option
spellings, the environment hooks, and how each output must match the reference
output. `/opt/pdf-tests-public/` — a sample corpus with expected answers
from the same reference checker, run with
`bash /opt/pdf-tests-public/run-public-tests.sh`; it covers only part of the milestone,
so passing it is not the whole milestone. `/app/WORKSPACE_HANDOFF.md` — keep it accurate.

## 3. Meta requirements

- The implementation budget is 7,200 seconds. Check `bash /app/timer.sh`; the configured timeout is authoritative and resets for this step.
- Work in the cumulative `/app` workspace and preserve the supplied documents and
  handoff files. Do not create a prose answer or an alternate entry point.
- Nothing that worked before may stop working. The new documents this milestone
  must handle are PDF/A conformance fixtures and a small malformed group.
- The utilities must build from source with `make` in a clean copy of `/app`,
  so leave it source-only: delete build products outside the `Makefile`'s own
  output directory, and compile scratch programs under `/tmp`. It must also survive
  being archived and unpacked, so keep the files you write to plain
  ASCII, non-ASCII bytes escaped inside string literals.
- Do not cheat by using online solutions or hints specific to this task.
- The built binaries must carry no third-party PDF engine markers. The
  strings `MuPDF`, `mupdf`, `Artifex`, `Ghostscript`, `poppler`, `pdfium`,
  `PDFium`, `libqpdf`, `qpdf version`, `xpdf`, `Xpdf`, `HarfBuzz`, and
  `FreeType` must not appear anywhere in any binary under `/app/build/` —
  including in `--help` or `--version` text copied from a man page. No
  symbol `readelf` lists in them may carry a third-party prefix (`fz_`,
  `FPDF`, `opj_`, `jbig2_`, `hb_`), whether it is exported or file-local. The JBIG2 prefix is
  the one most easily carried by accident: a codec's internal helper keeps its own
  name, and a file-local function still appears in the symbol table. None of
  these names arises in a library written from scratch; the adopted CLI names
  (`pdfinfo`, `pdftotext`, etc.) are fine.
- Work only from this workspace and the local references named above; do not
  search the system for other implementations or answers.
- Run the local checks named above and keep `/app/WORKSPACE_HANDOFF.md` an accurate
  description of the repository.
