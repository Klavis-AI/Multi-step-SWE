# PDF 2.0: AES-256 encryption, UTF-8 text strings, associated files and versions

## 1. Implement this milestone

Extend the implementation already in this workspace to ISO 32000-2 (PDF 2.0): the standard security
handler at revision 6 with AES-256, text strings encoded as UTF-8, the associated files an object may
point at, output intents carried by a page rather than by the document, and the constructs 2.0
deprecates. It is written in **C**, built by `make`, and nothing that read before may stop reading.

The set of interfaces does not grow. This milestone adds **no new program**: everything PDF 2.0
changes is reached through the six utilities the previous milestones built, and the only
addition anywhere is one further `qpdf` option, `--show-encryption-key`. `/app/TEST_INTERFACE.md`
fixes the build command, the option subset, the hook and the output each utility must reproduce.

Those six utilities are the only way any of this is reached. `make`, run in `/app`, must put them at
**`/app/build/pdfinfo`, `/app/build/pdftotext`, `/app/build/pdffonts`, `/app/build/qpdf`,
`/app/build/pdfimages` and `/app/build/pdfsig`** — that is where they are invoked from, so a library
that works but is not reachable at those six paths cannot be used at all. Keep all six building:
correct decryption is unusable behind a missing `Makefile`.

What must work is what `/app/SPEC/pdf20.md` sets out:

* **encryption** — the standard security handler at `/V 5` and `/R 6`, with a `/StdCF` crypt
  filter whose `/CFM` is `/AESV3`: AES-256 in CBC mode with the initialization vector in the first
  16 bytes, and **the file encryption key applied directly**, without the per-object derivation
  every earlier revision requires. `/U` and `/O` are 48 bytes and carry their own salts; `/UE` and
  `/OE` hold the file key wrapped under a key derived from a password; the hash behind all four is
  Algorithm 2.B, whose iterated hardening loop is the whole of what separates `/R 6` from the
  deprecated `/R 5`. `/Perms` holds `/P` and `/EncryptMetadata` encrypted under the file key and
  **shall be checked against them**: a document whose `/Perms` disagrees with its own dictionary is
  reported rather than opened in silence, and one whose `/UE` or `/Perms` is missing is malformed
  rather than unencrypted. Everything the previous milestone decrypted — RC4, AES-128, revisions 2,
  3 and 4 — shall still decrypt, in a 2.0 file as readily as in a 1.7 one;
* **text strings** — a text string may now be UTF-8 introduced by the three bytes `EF BB BF`,
  alongside the UTF-16BE form introduced by `FE FF` and PDFDocEncoding; the introducing bytes are
  not part of the value. This reaches every string that is a text string, and no string that is
  not one — the operand of a text-showing operator included;
* **associated files** — an `/AF` array of file specifications may hang off the catalogue, a page,
  a form XObject or an annotation, saying those files belong with that object, each specification
  carrying `/AFRelationship` to name the association. The embedded file stream, and the
  `/Names /EmbeddedFiles` tree that has always held it, are unchanged;
* **output intents** — a page may carry its own `/OutputIntents`, which the document-level array
  no longer has to carry alone, and an intent may name its destination profile indirectly through
  `/DestOutputProfileRef` as well as embedding it;
* **deprecated and removed constructs** — XFA, `/Movie` and `/Sound` annotations, the catalogue's
  `/Perms` with `/UR3`, and the RC4 ciphers. A reader does not enforce a deprecation: every one of
  these **shall still be read and reported exactly as written**, at a 2.0 header as at a 1.7;
* **version declaration** — the `%PDF-2.0` header, the catalogue's `/Version`, which overrides the
  header only when it is the later of the two, and the extensions dictionary, whose `/BaseVersion`
  and `/ExtensionLevel` are reported alongside the version.

A file declaring 1.7 in its header while using every structure above must still be read, and
painting a page is not required: what is required is what a reader reports.

**Not all of that is visible through these interfaces.** Three of the constructs above are
invisible to the reference tools whose output the utilities reproduce. `/app/TEST_INTERFACE.md`
§4.2 names them, gives the measurement behind each and says what the utilities expose of them
instead; read it before deciding where the work goes.

## 2. Read these local references

**Normative semantics:** `/app/SPEC/pdf20.md` — this milestone's requirements, and the only PDF 2.0
requirements text in this image: ISO 32000-2 is not shipped here, so it carries the whole normative
load for what is new. `/opt/pdfspec/ISO-32000-1-2008.txt` — the published ISO 32000-1:2008 text, the
underlying format that PDF 2.0 revises rather than replaces, and the source of every clause and table
number `pdf20.md` cites. `/app/SPEC/pdf16-17.md`, `pdf15.md`, `pdf14.md`, `pdf13.md` — still apply.

**Standards these clauses rely on**, copied into the image (each PDF original sits beside its `.txt`): `/opt/docs/agl/glyphlist.txt` — the Adobe Glyph List 2.0, which ISO 32000-1 9.10.2 consults to map a glyph name to Unicode (`aglfn.txt` beside it is its list for new fonts); `/opt/docs/fips/NIST.FIPS.197.txt` — FIPS PUB 197 (2001), the AES that ISO 32000-1 7.6.2 names; `/opt/docs/rfc/rfc2898.txt` — RFC 2898 (PKCS #5), the padding `pdf16-17.md` cites; `/opt/docs/fips/NIST.FIPS.180-4.txt` — FIPS PUB 180-4, the SHA-256, SHA-384 and SHA-512 that `pdf20.md`'s Algorithm 2.B uses.

**The interfaces' own documentation**, copied into the image:
`/opt/pdfspec/man-pdfinfo.txt`, `man-pdftotext.txt` and `man-pdffonts.txt` (the poppler-utils man
pages), `/opt/pdfspec/man-qpdf.txt` and `/opt/pdfspec/qpdf-running-qpdf.txt` (qpdf's man page and
the manual's command-line chapter, which defines every qpdf option used here,
`--show-encryption-key` included, and the exit status). This image has no man page for `pdfimages`
or `pdfsig`; `/app/TEST_INTERFACE.md` defines them, unchanged from the previous milestone.

**Additional files:** `/app/TEST_INTERFACE.md` — the build command, the option spellings, the
`SOURCE_DATE_EPOCH` hook, and how each output must match the reference output.
`/opt/pdf-tests-public/` — a sample corpus with expected answers from the same reference tools, run
with `bash /opt/pdf-tests-public/run-public-tests.sh`; it covers only part of the milestone, so
passing it is not the whole milestone. `/app/WORKSPACE_HANDOFF.md` — keep accurate.

## 3. Meta requirements

- The implementation budget is 7,200 seconds. Check `bash /app/timer.sh`; the configured timeout is authoritative and resets for this step.
- Work in the cumulative `/app` workspace and preserve the supplied documents and handoff files.
  Do not create a prose answer or an alternate entry point.
- Nothing that worked before may stop working: files within the previous milestones' scope must
  still read correctly.
- `/app` must build from source with `make` in a clean copy, so leave it source-only: delete build
  products outside the `Makefile`'s output directory, and compile scratch programs under `/tmp`. It
  must also survive being archived and unpacked: keep what you write plain ASCII, non-ASCII escaped in
  literals.
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
- Run the local checks named above and keep `/app/WORKSPACE_HANDOFF.md` an accurate description of the repository.
