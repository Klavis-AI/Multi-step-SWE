# PDF/A-3, PDF/A-4 and PDF/UA-1: conformance validation

## 1. Implement this milestone

Extend the implementation already in this workspace to the standards that finish this
line: ISO 19005-3 and ISO 19005-4, the later PDF/A archiving parts, and ISO 14289-1 —
PDF/UA-1, which asks not whether a file will survive an archive but whether its content is
described by a tagging structure at all. **Report** whether a file conforms at a requested
conformance level. It is written in **C**, built by `make`; everything that read correctly
before must still read now.

This milestone adds no new program: it is the previous milestone's validator reaching
further, `verapdf` gaining the flavours the later parts define. `/app/TEST_INTERFACE.md`
fixes the build command, the option subset, the environment hooks, and the output each utility must reproduce.

Those programs are the only way any of this is reached. `make`, run in `/app`, must put
**every** utility into `/app/build/` under its real program name: **`/app/build/pdfinfo`,
`/app/build/pdftotext`, `/app/build/pdffonts`, `/app/build/qpdf` and
`/app/build/verapdf`**, together with any the intervening milestones added, each keeping the
path it already had. That is where each of them is invoked from, so a library that works
but is not reachable at those paths cannot be used at all. Build the entry points first
and keep them building: even a complete milestone of correct conformance logic is unusable
behind a missing `Makefile`, or behind binaries left somewhere other than `/app/build/`.

What must work is what `/app/SPEC/pdfa3-4-ua.md` sets out, clause by clause:

* **PDF/A-3 validation**, at `3b` — the flag also accepts `3a` and `3u`, but validation
  at those levels is not required. Part 3 is part 2 with one door opened:
  a conforming file may embed a file of **any** format, where part 2 admits only
  embedded PDF/A. Its machinery carries the strand — `/AF` on the catalogue, a page or a
  structure element; `/AFRelationship` naming what an attachment is *to* the document;
  the `/F`, `/UF` and `/Subtype` entries that retrieve it. All of part 2 still applies;
* **PDF/A-4 validation**, at `4`, `4e` and `4f`. Part 4 is **based on PDF 2.0**, so it
  stands on the PDF 2.0 milestone's work: a conforming header is `%PDF-2.n`, and what 2.0
  added and deprecated is in force. The letters `a`, `b` and `u` are gone; `4e` permits
  engineering content, `4f` arbitrary embedded files. Identification moves with them:
  `pdfaid:part` 4, `pdfaid:rev` 2020, `pdfaid:conformance` only for `4e` and `4f`;
* **PDF/UA-1 validation**, at `ua1`, a different kind of requirement: whether the content
  is described by a structure hierarchy — a `/StructTreeRoot`; every mark on the page
  tagged as real content or marked as an artifact, never both; a role map terminating at
  standard types without a cycle; `/Alt` or `/ActualText` on a figure; a well-formed
  `/Lang`; `/Tabs /S`; `dc:title` with `/DisplayDocTitle` true; and tables, lists and
  headings nested as the model requires. SPEC §3 is clause by clause, and **§3.12 says what
  no checker can decide** — whether an `/Alt` really describes its figure, whether tagged
  order is intended reading order — which no implementation is required to decide;

## 2. Read these local references

**Normative semantics:** `/app/SPEC/pdfa3-4-ua.md` — the clauses of ISO 19005-3:2012,
ISO 19005-4:2020 and ISO 14289-1:2014 this milestone covers. `/app/SPEC/pdfa1-2.md` — the
previous milestone's clauses, which parts 3 and 4 restate rather than replace and which
still apply in full. `/opt/pdfspec/ISO-32000-1-2008.txt` — the published ISO 32000-1:2008
text, the underlying file format and the document ISO 14289-1 names for the tagging model.
`/app/SPEC/pdf13.md`, `pdf14.md`, `pdf15.md`, `pdf16-17.md` and `pdf20.md` — the earlier
milestones', still in force, `pdf20.md` especially: ISO 19005-4 is built on PDF 2.0.

**Standards these clauses rely on**, copied into the image (each PDF original sits beside its `.txt`): `/opt/docs/agl/glyphlist.txt` — the Adobe Glyph List 2.0, which ISO 32000-1 9.10.2 consults to map a glyph name to Unicode (`aglfn.txt` beside it is its list for new fonts); `/opt/docs/fips/NIST.FIPS.197.txt` — FIPS PUB 197 (2001), the AES that ISO 32000-1 7.6.2 names; `/opt/docs/rfc/rfc2898.txt` — RFC 2898 (PKCS #5), the padding `pdf16-17.md` cites; `/opt/docs/fips/NIST.FIPS.180-4.txt` — FIPS PUB 180-4, the SHA-256, SHA-384 and SHA-512 that `pdf20.md`'s Algorithm 2.B uses; `/opt/docs/xmp/XMPSpecificationPart1.txt` and `XMPSpecificationPart2.txt` — Adobe's XMP specification, the data model and packet of part 1 and the standard schemas and their properties of part 2 (the 2012 edition, which revises the September 2005 one ISO 32000-1 cites); `/opt/docs/icc/ICC1V42.txt` — ICC.1:2004-10, the ICC profile format ISO 32000-1 cites; `/opt/docs/rfc/rfc3066.txt` — RFC 3066, the language tags `pdfa3-4-ua.md` cites.

**The interfaces' own documentation**, copied into the image:
`/app/SPEC/verapdf-cli.txt`, the veraPDF command-line usage, defining
`--flavour`, `--format` and the exit status. The poppler man pages and `qpdf-running-qpdf.txt` define the rest.

**Additional files:** `/app/TEST_INTERFACE.md` — the build command, the option spellings,
the environment hooks, and how each output must match the reference
output. `/opt/pdf-tests-public/` — a sample corpus with expected answers from the same
reference checker, run with `bash /opt/pdf-tests-public/run-public-tests.sh`; it covers
only part of the milestone, so passing it is not the whole milestone. `/app/WORKSPACE_HANDOFF.md` — keep accurate.

## 3. Meta requirements

- The implementation budget is 7,200 seconds. Check `bash /app/timer.sh`; the configured timeout is authoritative and resets for this step.
- Work in the cumulative `/app` workspace and preserve the supplied documents and handoff
  files. Do not create a prose answer or an alternate entry point.
- Nothing that worked before may stop working.
- The utilities must build from source with `make` in a clean copy of `/app`,
  so leave it source-only: delete build products outside the `Makefile`'s own output
  directory, and compile scratch programs under `/tmp`. It must also survive being archived
  and unpacked, so keep the files you write to plain ASCII, non-ASCII bytes
  escaped inside string literals.
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
- Run the local checks named above and keep `/app/WORKSPACE_HANDOFF.md` an accurate description of
  the repository.
