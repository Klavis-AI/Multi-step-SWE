# Rendering a page to raster, and writing linearized output

## 1. Implement this milestone

Extend the implementation already in this workspace to the two halves that finish this line:
**paint** a page — the shadings, the blend modes and the soft masks the earlier milestones
could describe but never had to draw — and **write** a file whose first page can be shown
before the rest has arrived. It is written in **C**, built by `make`. This is the last
milestone, so everything that worked before must still work. The new documents this
milestone must handle fall into five groups, `raster`, `shading`, `composite`,
`linearize` and `damaged`.

This milestone adds one program and two options: **`pdftoppm`**, the poppler-utils rasterizer,
adopted as-is exactly as the other interfaces here were, and `--linearize` and
`--check-linearization` on the `qpdf` this workspace already builds. `/app/TEST_INTERFACE.md`
fixes the build command, the option subset, the frozen render command line, the environment
hooks and the output each utility must reproduce.

Those programs are the only way any of this is reached. `make`, run in `/app`, must put
**every** utility into `/app/build/` under its real program name: `/app/build/pdfinfo`,
`/app/build/pdftotext`, `/app/build/pdffonts`, `/app/build/qpdf`, `/app/build/pdfimages`,
`/app/build/pdfsig` and `/app/build/verapdf`, together with any the
intervening milestones added, each keeping the path it already had — and this milestone's
**`/app/build/pdftoppm`**. That is where each of them is invoked from, so a library that
renders correctly but is not reachable at those paths cannot be used at all. Build the
entry points first and keep them building: even a complete milestone of correct rasterization
is unusable behind a missing `Makefile`, or behind binaries left somewhere other than
`/app/build/`.

What must work is what `/app/SPEC/raster-linearize.md` sets out, clause by clause:

* **rasterization** — a page painted into an 8-bit RGB raster at the one resolution
  `/app/TEST_INTERFACE.md` §2.1 freezes, the page's own geometry deciding the pixel grid.
  **What must match is axis-aligned rectangular fills**, which the reference snaps rather
  than antialiases: each edge moves to the nearest pixel boundary, so a pixel is painted, at
  full coverage, exactly when its centre lies inside the rectangle
  (`/app/SPEC/raster-linearize.md` §1.3). Every such edge lies at least 0.2 device px from
  a pixel centre, so the nearest boundary is never a tie. Glyphs, strokes, clips and images are
  described in the SPEC because a renderer meets them, but rendering them is not required:
  `/app/SPEC/raster-linearize.md` §1.3 records why (a clip antialiases at quarter-pixel
  granularity and a zero-extent rect paints a line), so they may be implemented or
  omitted;
* **shadings** — all seven types are described in the SPEC because the shading dictionary is
  one interface, but **only type 2 is required**: the continuous axial ramp,
  bounded by `/Extend` rather than by a clip, painted through `sh` and through a shading
  pattern. Radial (type 3) ramps, banded stitching shadings and clip-bounded shadings are
  not required, a choice made by measurement for the reasons
  `/app/SPEC/raster-linearize.md` §2 records;
* **transparency** — constant alpha, the blend modes, and a soft mask whose luminosity or alpha
  modulates what is painted through it, composited into transparency groups. The earlier
  milestones read these dictionaries; this one obeys them;
* **linearized output** — `qpdf --linearize` writing a file in the form Annex F defines: the
  parameter dictionary in the first bytes, the first page's objects ahead of the rest, and two
  cross-reference sections. A hint stream must be present and its offsets self-consistent;
  **the hint tables' bit fields are not required to decode exactly**: the requirement there
  is only that the file not contradict itself.
  `/app/SPEC/pdf14.md` §5 already defines *reading* this form and still applies unchanged; what
  is new is producing one, the much harder direction;
* **reporting on linearization** — `qpdf --check-linearization`, which says whether a file
  carries linearization data and whether that data agrees with the file that carries it.

**Your raster output must match the reference exactly.** Not by image similarity and not within a
tolerance: the reference is a rendering by a real implementation, and yours either matches it
or does not. An exact match is attainable because nothing in this pipeline records
anything that varies — the image file carries no timestamp, producer name or comment — and the
same page rendered in separate containers and under ten environment settings produced identical
bytes every time. `/app/TEST_INTERFACE.md` §4.2 gives the measured reason no averaged similarity
measure can replace this, and §2.1 there freezes every render option that changes bytes.

## 2. Read these local references

**Normative semantics:** `/app/SPEC/raster-linearize.md` — the clauses of ISO 32000-1:2008 this
milestone covers: the graphics and colour model, shadings, transparency, and Annex F. It also
fixes the rendering interface in full, because no man page for that program ships in this
image. `/opt/pdfspec/ISO-32000-1-2008.txt` — the published ISO 32000-1:2008 text, the normative
definition of all of it. `/app/SPEC/pdf13.md` through `pdf20.md`, `pdfa1-2.md` and
`pdfa3-4-ua.md` — the earlier milestones' clauses, all still in force; `pdf14.md` defines the
transparency constructs and the linearized form as a reader sees them.

**Standards these clauses rely on**, copied into the image (each PDF original sits beside its `.txt`): `/opt/docs/agl/glyphlist.txt` — the Adobe Glyph List 2.0, which ISO 32000-1 9.10.2 consults to map a glyph name to Unicode (`aglfn.txt` beside it is its list for new fonts); `/opt/docs/fips/NIST.FIPS.197.txt` — FIPS PUB 197 (2001), the AES that ISO 32000-1 7.6.2 names; `/opt/docs/rfc/rfc2898.txt` — RFC 2898 (PKCS #5), the padding `pdf16-17.md` cites; `/opt/docs/fips/NIST.FIPS.180-4.txt` — FIPS PUB 180-4, the SHA-256, SHA-384 and SHA-512 that `pdf20.md`'s Algorithm 2.B uses; `/opt/docs/xmp/XMPSpecificationPart1.txt` and `XMPSpecificationPart2.txt` — Adobe's XMP specification, the data model and packet of part 1 and the standard schemas and their properties of part 2 (the 2012 edition, which revises the September 2005 one ISO 32000-1 cites); `/opt/docs/icc/ICC1V42.txt` — ICC.1:2004-10, the ICC profile format ISO 32000-1 cites; `/opt/docs/rfc/rfc3066.txt` — RFC 3066, the language tags `pdfa3-4-ua.md` cites.

**The interfaces' own documentation**, copied into the image:
`/opt/pdfspec/qpdf-running-qpdf.txt`, the qpdf manual's command-line chapter, which defines
`--linearize`, `--check-linearization` and the exit status; and `/opt/pdfspec/man-pdfinfo.txt`,
`man-pdftotext.txt` and `man-pdffonts.txt`, the poppler-utils man pages for the earlier
programs.

**Additional files:** `/app/TEST_INTERFACE.md` — the build command, the option spellings, the
frozen render command, the environment hooks, and how each output must match the reference
output. `/opt/pdf-tests-public/` — a sample corpus with expected answers from
the same reference tools, run with `bash /opt/pdf-tests-public/run-public-tests.sh`; it covers
only part of the milestone, so passing it is not the whole milestone. `/app/WORKSPACE_HANDOFF.md` — keep it accurate.

## 3. Meta requirements

- The implementation budget is 7,200 seconds. Check `bash /app/timer.sh`; the configured timeout is authoritative and resets for this step.
- Work in the cumulative `/app` workspace and preserve the supplied documents and handoff
  files. Do not create a prose answer or an alternate entry point.
- Nothing that worked before may stop working.
- The utilities must build from source with `make` in a clean copy of `/app`, so
  leave it source-only: delete build products outside the `Makefile`'s own output directory,
  and compile scratch programs under `/tmp`. It must also survive being archived and
  unpacked, so keep the files you write to plain ASCII, non-ASCII bytes escaped inside
  string literals.
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
- Run the local checks named above and keep `/app/WORKSPACE_HANDOFF.md` an accurate description of the
  repository.
