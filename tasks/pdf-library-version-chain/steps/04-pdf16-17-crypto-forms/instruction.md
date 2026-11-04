# PDF 1.6/1.7: encryption, image codecs, form fields and signature structures

## 1. Implement this milestone

Extend the implementation already in this workspace to PDF 1.6 and 1.7: the standard
security handler with AES-128 and RC4, the `JBIG2Decode` and `JPXDecode` filters applied
rather than carried past, interactive form fields read, filled and flattened, and the
structures a digital signature is made of. It is written in **C** and built by `make`,
and everything that read correctly before must still read correctly now.

The interfaces that expose it grow by two: `pdfimages` and `pdfsig`, poppler-utils programs
adopted as-is on the terms the other four were. `qpdf` also reaches further -- the encryption
parameters, a decrypted copy, and two options that write a file rather than a report.
`/app/TEST_INTERFACE.md` fixes the build command, the option subset, the hook and the output each utility must reproduce.

Those six utilities are the only way any of this is reached. `make`, run in `/app`, must
put them at **`/app/build/pdfinfo`, `/app/build/pdftotext`, `/app/build/pdffonts`,
`/app/build/qpdf`, `/app/build/pdfimages` and `/app/build/pdfsig`** -- that is where they
are invoked from, so a library that works but is not reachable at those six paths cannot
be used at all. Build the six entry points first and keep them building: even a complete
milestone of correct decryption is unusable behind a missing `Makefile`, or behind binaries
left elsewhere than `/app/build/`. The two new programs must work
on every document, not only those carrying their own constructs, so neither may be
deferred until its own construct works.

What must work is what `/app/SPEC/pdf16-17.md` sets out against ISO 32000-1:2008:

* **encryption** -- the standard security handler and the documents it protects: `/V 4`
  with a `/StdCF` crypt filter whose `/CFM` is `/AESV2` (AES-128 in CBC mode, the
  initialization vector the first 16 bytes of the data) or `/V2` (RC4), the older `/V 1`
  and `/V 2` RC4 forms, revisions `/R` 2, 3 and 4, the file key and the per-object key,
  `/O`, `/U` and password authentication, the `/P` permission bits, `/EncryptMetadata`.
  Decryption is transparent to everything above it -- a page extracts as ordinary text and
  a stream's decoded data is its plaintext -- while the encryption dictionary is itself
  never encrypted. A document whose user password is empty opens with none supplied; one
  whose user password is not empty opens when either of the two is given;
* **image codecs** -- `JBIG2Decode` and `JPXDecode` recognised on an image XObject rather
  than carried through blindly. What is required is what the interfaces report about such an
  image and what they refuse to do with it: the `pdfimages -list` row, which needs the
  codestream header parsed for the image's real dimensions, colour space and bit depth;
  `pdfimages -jbig2`, which writes out the embedded codestream and the global segments
  `/DecodeParms` names as separate files; and `--filtered-stream-data`, which **refuses**
  over a stream in either filter and exits 2 having written nothing. **Producing the decoded
  samples is NOT required** -- nothing here needs a decoded image, so a full JPEG2000 or
  JBIG2 decoder is not needed. A
  payload that is not valid data of its kind is not a file-level error: the document still
  reads;
* **interactive forms** -- the `/AcroForm` dictionary, the field hierarchy and a terminal
  field's value. A text field's value is part of what the page shows: where the widget has no
  appearance stream the appearance shall be constructed from `/V`, `/DA` and the default
  resources, and where it has one, `/NeedAppearances` decides which wins -- the stored
  appearance when absent or false, the current value when true. A hidden widget shows nothing.
  Flattening moves it into the page's content stream, leaving no annotations and no form;
* **signatures** -- the signature field, the signature dictionary that is its value, and
  `/Filter`, `/SubFilter`, `/ByteRange`, `/Contents` and a `/Reference` naming a transform
  method, all read and reported exactly as the file gives them, a `/SubFilter` name meaning
  nothing to you included; a signature field with no value shall be reported as the unsigned
  field it is. **Nothing here verifies a signature**: no digest is computed, no certificate is
  consulted, and a `/ByteRange` is reported as written rather than checked against the file.

A file declaring an earlier version in its header while using every structure above must still
be read, and painting a page is not required: what is required is what a reader reports.

## 2. Read these local references

**Normative semantics:** `/opt/pdfspec/ISO-32000-1-2008.txt` -- the published ISO
32000-1:2008 text, the definition of the file format to implement. `/app/SPEC/pdf16-17.md`
-- the clauses this milestone covers; `/app/SPEC/pdf15.md`, `/app/SPEC/pdf14.md` and
`/app/SPEC/pdf13.md` -- the previous milestones', all of which still apply.

**Standards these clauses rely on**, copied into the image (each PDF original sits beside its `.txt`): `/opt/docs/agl/glyphlist.txt` — the Adobe Glyph List 2.0, which ISO 32000-1 9.10.2 consults to map a glyph name to Unicode (`aglfn.txt` beside it is its list for new fonts); `/opt/docs/fips/NIST.FIPS.197.txt` — FIPS PUB 197 (2001), the AES that ISO 32000-1 7.6.2 names; `/opt/docs/rfc/rfc2898.txt` — RFC 2898 (PKCS #5), the padding `pdf16-17.md` cites.

**The interfaces' own documentation**, copied into the image:
`/opt/pdfspec/man-pdfinfo.txt`, `man-pdftotext.txt`, `man-pdffonts.txt` (the poppler-utils
man pages) and `/opt/pdfspec/qpdf-running-qpdf.txt`
(the qpdf manual's command-line chapter, which defines every qpdf option used here, the
encryption and form options included, and its exit status).

**Additional files:** `/app/TEST_INTERFACE.md` -- the build command, the option spellings,
the `SOURCE_DATE_EPOCH` hook, and how each output must match the reference
output. `/app/tests-public/` -- **this milestone's** sample corpus with expected answers from the
same reference tools, run with `bash /app/tests-public/run-public-tests.sh`; it covers
only part of the milestone, so passing it is not the whole milestone. `/opt/pdf-tests-public/` holds the first
milestone's sample and still runs, as a regression check on earlier behaviour. `/app/WORKSPACE_HANDOFF.md` -- keep accurate.

## 3. Meta requirements

- The implementation budget is 7,200 seconds. Check `bash /app/timer.sh`; the configured timeout is authoritative and resets for this step.
- Work in the cumulative `/app` workspace and preserve the supplied documents and handoff
  files. Do not create a prose answer or an alternate entry point.
- Nothing that worked before may stop working: files within the previous milestones'
  scope must still read correctly.
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
  name, and the rule covers the whole symbol table, not the exported half of
  it, so even a `static` function in a file written from scratch must not carry it.
  None of these names arises in a library written from scratch otherwise; the
  adopted CLI names (`pdfinfo`, `pdftotext`, etc.) are fine.
- Work only from this workspace and the local references named above; do not
  search the system for other implementations or answers.
- Run the local checks named above and keep `/app/WORKSPACE_HANDOFF.md` an accurate description of
  the repository.
