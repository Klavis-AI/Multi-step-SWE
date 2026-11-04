# TEST_INTERFACE.md — how this repository is built and driven, and how its output is compared

This file is authoritative for the mechanics of this workspace: the build command, the
programs and option spellings in use, the one environment hook, and exactly
how each output is compared with the reference.

## 1. Build

`make`, run in `/app`, builds every utility into `/app/build/` under its real program
name. That exact command — plain `make`, no added flags, with `/app` as the working
directory — is how the utilities are built:

```
make            # -> /app/build/pdfinfo, /app/build/pdftotext, /app/build/pdffonts,
                #    /app/build/qpdf, /app/build/pdfimages, /app/build/pdfsig
make clean
```

`pdfimages` and `pdfsig` are new at this milestone and are built by the same `make`, into
the same directory, under those exact names. All six must work on every document, not only on those
carrying their own constructs.

The utilities are invoked with `/app` as their working directory, and are given a
`PATH` containing only `/app/build`, so they must not depend on any other program
being reachable by name. Each utility is self-contained: it starts no other program, opens no network
connection, and loads no code beyond its own binary and the C runtime.

They are also run with `LC_ALL=C.UTF-8`. Do not depend on the ambient locale for case
folding, collation, or number formatting; this one is pinned, and output is compared as bytes.

The workspace must not contain the *sources* of third-party PDF engines — MuPDF, pdfium,
poppler, qpdf and HarfBuzz, each recognized by its own public symbol names. This rule is
about vendoring someone else's engine, not a restriction on your naming: it matches whole
identifiers, ignores comments, and covers no name a library written from scratch would
arrive at on its own. Writing your own `pdf_load_page()` is fine; shipping MuPDF's
`fz_open_document()` is not. The same rule reaches the cryptography and the codecs this
milestone adds: write your own AES, RC4, MD5, JBIG2 and JPEG2000 code, or take it from a
general-purpose library you may already link, but do not import another PDF engine's.

The **built binaries** are covered as well, separately from the sources: in
`/app/build/`'s utilities, `strings` must find none of these vendor markers
(`MuPDF`, `Artifex`, `Ghostscript`, `poppler`, `pdfium`, `libqpdf`, `qpdf version`, `xpdf`,
`HarfBuzz`, `FreeType`), and no symbol `readelf` lists may carry one of these
third-party prefixes (`fz_`, `FPDF`, `opj_`, `jbig2_`, `hb_`). Unlike the source rule, the
marker rule applies **anywhere in the binary**, including data sections — because a
`Makefile` recipe could fetch a prebuilt engine and install it as `/app/build/pdfinfo`, which
would satisfy every other rule. None of those markers is a name a library written from
scratch produces on its own.


The implementation language is **C** (C11). The image provides `gcc`, `make`,
`binutils`, `gdb` and `valgrind`, plus `zlib`.

The utilities must be compiled ELF binaries produced by that `make`, and
each one's dynamic dependencies must lie within the C runtime and zlib:

```
libc.so  libm.so  libz.so  libgcc_s.so  libpthread.so  libdl.so  ld-linux
```

No utility may be a script, link anything outside that list, or be byte-identical to
another binary shipped in the workspace. Static linking is fine. Each utility must be a
regular file: a symbolic link in a utility's place is removed before the utilities are run,
and that utility counts as not built. The workspace must build from source with `make` in a
container that has a C toolchain and **no PDF implementation of any kind** — no poppler, no
qpdf, no mupdf, no ghostscript, no pdfium, no PDF module for python — and no independent
image codec either. A utility that calls out to a system PDF tool or to a system JPEG2000
or JBIG2 decoder therefore finds none to call.

Leave `/app` source-only. The workspace is archived and restored when it is
handed on, so delete build products outside `build/` before you
finish, and compile scratch programs under `/tmp`.

## 2. The utilities and their options

The programs are **not** this task's invention. Each is an existing published
command-line interface adopted as-is, with the same program name, the same option
letters, the same operand order and the same output layout:

* `pdfinfo`, `pdftotext`, `pdffonts`, `pdfimages`, `pdfsig` — **poppler-utils**, as
  defined by the `pdfinfo(1)`, `pdftotext(1)`, `pdffonts(1)`, `pdfimages(1)` and
  `pdfsig(1)` man pages.
* `qpdf` — the **qpdf** command-line tool, as defined by the qpdf manual
  ("Running qpdf").

Only the options listed below are exercised. Behaviour for all of them is the
behaviour those documents describe. Everything above the rule was already licensed at a
previous milestone; everything below it is new here:

```
pdfinfo   [-f N] [-l N] FILE
pdftotext [-f N] [-l N] [-enc ENC] FILE [OUTFILE|-]
pdffonts  [-f N] [-l N] FILE
qpdf --show-object={trailer|N[,G]} [--filtered-stream-data | --raw-stream-data] FILE
qpdf --check FILE
qpdf --show-xref FILE
qpdf --show-pages FILE
qpdf --show-linearization FILE
--------------------------------------------------------------------------------
pdfinfo   [-f N] [-l N] [-opw PW] [-upw PW] FILE
pdftotext [-f N] [-l N] [-enc ENC] [-opw PW] [-upw PW] FILE [OUTFILE|-]
pdffonts  [-f N] [-l N] [-opw PW] [-upw PW] FILE
pdfimages -list [-f N] [-l N] [-opw PW] [-upw PW] FILE
pdfimages [-jbig2] [-f N] [-l N] [-opw PW] [-upw PW] FILE IMAGE-ROOT
pdfsig FILE
qpdf --show-encryption FILE
qpdf --is-encrypted FILE
qpdf --decrypt FILE {OUTFILE|-}
qpdf --generate-appearances [--flatten-annotations={all|print|screen}] FILE OUTFILE
qpdf --flatten-annotations={all|print|screen} FILE OUTFILE
```

`--password=PW` may accompany **any** `qpdf` invocation in this list and is the qpdf
spelling of what `-upw`/`-opw` do for the poppler programs: one option, tried as both
passwords. Exactly one qpdf inspection option is given per invocation; the inspection
options are never combined with each other. `--generate-appearances` and
`--flatten-annotations` are the one licensed combination, and in that order.

`pdfimages` with no format option writes netpbm files; `-list` prints the image table
instead of writing anything and takes no `IMAGE-ROOT` operand. `-jp2`, `-png`, `-tiff`,
`-j`, `-ccitt`, `-all`, `-p` and `-q` are part of the published interface and **no
case uses them**: raw JPEG2000 passthrough was covered at the previous milestone through
`qpdf --raw-stream-data`, and is not repeated here. `pdfsig` is invoked with no
options at all; its signing and validation options require a certificate database that
does not exist in either image.

Page numbers on the command line are 1-based. `-` as `pdftotext`'s or `qpdf --decrypt`'s
output operand writes to standard output. Diagnostics go to standard error, never to
standard output. The only text encoding exercised is `-enc UTF-8`, which is also
`pdftotext`'s default; no other `ENC` value is used.

**Exit status.** For `pdfinfo`, `pdftotext`, `pdffonts` and `pdfimages` it is compared
exactly, not merely as zero versus non-zero: **0** on success, and poppler's own value
when it fails -- **1** when the PDF could not be opened, **2** when the output file
could not be. The matching value is required. Four rules are new here, and all
four are required:

* a **wrong or missing password** is exit **1** from the poppler programs and exit **2**
  from `qpdf`. On a file that is not encrypted, a password argument is ignored entirely
  and the status is 0, so supplying one is not by itself evidence of anything;
* **`qpdf --is-encrypted`** is a boolean carried by the status alone: **0** for an
  encrypted file, **2** for one that is not. The 2 here does not mean an error was found
  and nothing is written to standard output;
* **`pdfsig`** exits **2** when, and only when, the file contains no signature at all,
  printing `File 'FILE' does not contain any signatures`. It exits **0** for every file
  that contains a signature field, however malformed that field's contents are;
* **`qpdf --flatten-annotations`** exits **3**, having written a file, when the document's
  `/NeedAppearances` is true: it declines to flatten appearances it has been told are out
  of date. Run after `--generate-appearances` the same file exits 0.

`qpdf`'s three documented values continue to be exercised as before: **0**
when nothing was found wrong, **3** when the operation succeeded but warnings were
issued, **2** when errors were found — including the previous milestone's **2** from
`--filtered-stream-data` over a stream whose filter chain is not decoded, which now
covers `JBIG2Decode` as well.

**`pdfimages` exits 0 even when the codec fails completely.** A corrupt, truncated or
entirely non-image payload produces a file on disk, a diagnostic on standard error, and
status 0. Exit status is never a decode-success signal at this milestone, and no
case treats it as one.

## 3. Environment hooks

There is exactly one:

* **`SOURCE_DATE_EPOCH`** (the reproducible-builds.org specification) — when set, any
  timestamp a utility writes into a file it produces uses that instant instead of the
  wall clock. It is always set when the utilities run. The rest of their environment is
  pinned as well -- `PATH` to the build directory, `LC_ALL=C.UTF-8`, and `HOME` and
  `TMPDIR` to `/tmp` -- so that a utility writing a temporary file behaves exactly as it
  does under your own `make && run-public-tests.sh`. None of those four selects behaviour; the
  hook above is the only thing in this workspace that does. It now covers the PDF files `--decrypt`,
  `--generate-appearances` and `--flatten-annotations` write, as well as the image files
  `pdfimages` writes.

Input and output paths are the operands the interfaces above already define. There is
no mode flag, no profile, no compatibility switch, no version banner and no marker
string in this workspace; behaviour is selected by the options above and by the
content of the PDF file.

## 4. What the output must match

The utilities are run over a corpus of PDF files, and their output must match answers
recorded from real independent implementations. The cases fall into these groups:

| group | what one case is |
| --- | --- |
| `encrypt` | one command over one encrypted file that opens with the empty password: the `Encrypted:` field of `pdfinfo`, one `qpdf --show-encryption` report, one `qpdf --show-object=N` of the encryption dictionary, one `--is-encrypted` status, or the text and stream data recovered from the document |
| `acroform` | one command over one file carrying an interactive form: a page of `pdftotext`, the `Form:` field of `pdfinfo`, one `qpdf --show-object=N` of a form or field dictionary, or one file produced by a fill-and-flatten run and what the utilities then report of it |
| `codec` | one command over one file whose image XObject names `JPXDecode` or `JBIG2Decode`: one `pdfimages -list` table, or one extracted JBIG2 codestream. Decoded pixels are not compared — see §4.1 |
| `signature` | one `pdfsig` report, or one `qpdf --show-object=N` of a signature dictionary or signature field, over a file that does or does not carry a signature |
| `password` | one command over one file that does not open with the empty password, or whose `/U` is corrupt: the exit status, and the answer where one is expected |

Every case belongs to exactly one group. The group is a property of the corpus file,
not of the command, so the same `pdftotext` page is counted under `encrypt` for an
encrypted file, under `acroform` for a file carrying a filled field, and under
`password` for a file that needs a password to open; it is never counted twice. A file
that is both encrypted and carries a form belongs to whichever family the corpus assigns
it, and the corpus assigns each file exactly one.

The corpus also contains files within the previous milestones' scope, compared by those
milestones' rules: what worked there must keep working.

### 4.1 Exactly how outputs are compared

Unless a rule below says otherwise, the compared text is the utility's **standard
output only**, and the exit status must match as well.

**Expectations are generated, never written by hand.** Every expectation in this
milestone is recorded by running the reference tools — poppler-utils 22.12.0 and
**qpdf 11.3.0** — over the corpus file, and for a file some option produced, by running
the whole sequence end to end under those same tools. This matters most for
`--show-object`, whose output is not a transcription of the file's bytes: it **re-sorts
keys into ASCII order**, it **deletes a key whose value is null**, and it **deletes a key
whose indirect reference does not resolve**, all silently and at exit 0. It matters again
for every report in this section whose exact wording, column layout and line order are
the reference tool's own. A hand-written expectation will disagree with the reference on
files that are perfectly correct, so none is used.

* **text** (`encrypt`, `acroform`, `password`) — compared as a *character sequence*, not
  as a layout. Both sides are normalized: Unicode NFC, soft hyphen `U+00AD` removed,
  control characters replaced by a space, every run of whitespace collapsed to one space,
  leading and trailing whitespace stripped. Where the line breaks and how many spaces
  fall between words therefore do not matter; which characters the page contains, and in
  what order, does. Two things ride on this rule at this milestone. For an encrypted file
  it is the whole of the decryption signal: the corpus pairs a document with its
  unencrypted twin, the page text is the same on both sides, and a wrong per-object key
  produces mojibake or nothing rather than a diagnostic. For a form file **the value of a
  filled field is part of the page text**, because the appearance is constructed where the
  file does not supply one; the corpus pairs two files differing only in
  `/NeedAppearances` and the expected answer is which of the stored appearance and the
  current value appears in the text.
* **info** (`encrypt`, `acroform`) — only the fields below are compared, as `Key: value`
  lines in this order, each value with internal whitespace runs collapsed. A field the
  report does not carry for a given file is simply absent from both sides:

  ```
  Title, Author, Creator, Producer, CreationDate, ModDate, Custom Metadata,
  Metadata Stream, Tagged, UserProperties, Suspects, Form, JavaScript, Pages,
  Encrypted, Page size, Page rot, File size, Optimized, PDF version
  ```

  `Subject` and `Keywords` are not compared. Lines outside this list are ignored, so
  emitting them is harmless. The list is the first milestone's, unchanged, but two of
  its fields are where this milestone's work shows. `Encrypted` is `no`, or `yes` followed by a
  parenthesised list — `yes (print:yes copy:yes change:yes addNotes:yes algorithm:AES)` —
  in which each permission word tracks the corresponding `/P` bit and **`algorithm:` is
  the discriminator between AES and RC4**, the only place either tool names the cipher
  outside `qpdf`. `Form` is `AcroForm` or `none`, and it is `none` again after a document
  has been flattened. A document that cannot be opened at all has no `Encrypted:` line and
  fails by its status instead.
* **encryption** (`encrypt`, `password`) — `qpdf --show-encryption`, and the same block
  where `qpdf --check` prints it inline. Lines are compared in order with whitespace runs
  collapsed:

  ```
  R = N
  P = N
  User password = ...
  Supplied password is owner password        (and/or: ... is user password)
  extract for accessibility: allowed|not allowed
  extract for any purpose: ...
  print low resolution: ...
  print high resolution: ...
  modify document assembly: ...
  modify forms: ...
  modify annotations: ...
  modify other: ...
  modify anything: ...
  stream encryption method: AESv2|RC4|none
  string encryption method: ...
  file encryption method: ...
  ```

  The nine permission lines are derived from `/P` and `/R` together, not from `/P` alone.
  The three `encryption method:` lines are emitted **whenever `/V` is 4 or greater**, which
in this milestone means the `/V 4` documents, and their
  absence is as much a part of the answer as their content: a `/V 2` RC4 document and a
  `/V 4` document whose `/CFM` is `/V2` are both RC4, and this is the only place they are
  told apart. `--show-encryption` over a file whose `/U` does not authenticate still exits
  0 and prints the block with `Incorrect password supplied` before it.
* **object** (all five groups) — `qpdf --show-object`. Compared with whitespace runs
  collapsed to one space and leading and trailing whitespace stripped, so the line wrap
  and the two spaces in the stream header `Object is stream. Dictionary:` do not matter.
  The previous milestones' four properties still hold — ASCII key re-sorting, the two
  silent deletions, reals passed through verbatim, escapes and empty containers preserved
  — and three are added here:
  1. strings and stream data come back **decrypted**. `--filtered-stream-data` over a
     content stream in an encrypted document is plaintext, and a string in an object
     dictionary prints as its plaintext;
  2. the **encryption dictionary is reported as it stands**, because its own strings are
     never encrypted. `/O` and `/U` print as hex strings, and this is the only command
     that reveals `/CF`, `/StmF`, `/StrF` and `/EncryptMetadata` at all;
  3. `--filtered-stream-data` over a `JBIG2Decode` or `JPXDecode` stream **refuses** at
     exit 2 with nothing on standard output, exactly as the previous milestone required
     for `JPXDecode` alone. Decoding those codecs is `pdfimages`' job at this milestone,
     not this option's, and a partial answer here is a wrong answer.
* **imagelist** (`codec`) — `pdfimages -list`. The column header line and the dashed rule
  beneath it are ignored; the remaining rows are compared in order with whitespace runs
  collapsed to one space. Every column is compared, and the row is fixed by the file
  rather than chosen by the implementation: `page`, `num`, `type`, `width`, `height`,
  `color`, `comp`, `bpc`, `enc`, `interp`, `object ID`, `x-ppi`, `y-ppi`, `size`, `ratio`.
  The **`enc` column** is this group's headline — it reads `jpx`, `jbig2`, `jpeg` or
  `image` — and it reports which filter was recognised *without decoding anything*, so it
  is compared for payloads that decode and payloads that do not alike. `size` and `ratio`
  are derived from the stored stream length and the decoded sample count, so they are
  determined by the file too. A file with no images prints the header and the rule and
  nothing else, at exit 0, and that is a real answer.
* **decoded pixels are NOT compared.** `pdfimages FILE IMAGE-ROOT` writes image files, and
  neither their sample bytes nor their names are compared. Pixel decoding of `JPXDecode` and
  `JBIG2Decode` payloads is outside this milestone: what is required is that the filter is
  **recognised** (the `enc` column of `imagelist`, which reports the filter without decoding
  anything, for payloads that decode and payloads that do not alike) and that a JBIG2
  **codestream is extracted byte-exactly**, with `/JBIG2Globals` split into its own file.
  Those are this group's two obligations.


* **codestream** (`codec`) — `pdfimages -jbig2 FILE IMAGE-ROOT`. Compared as the set of
  file names produced plus the SHA-256 of each. The embedded stream is written out
  untouched, byte for byte, and where `/DecodeParms` names a `/JBIG2Globals` stream the
  reference writes **two** files for the one image — `ROOT-NNN.jb2e` for the page segments
  and `ROOT-NNN.jb2g` for the globals — and both are compared. Splitting the globals into
  their own file is the obligation: an implementation that concatenates them, omits
  them, or writes them into the page file produces the wrong set of names or the wrong
  bytes.
* **signature report** (`signature`) — `pdfsig`. Lines are compared in order with
  whitespace runs collapsed, and **only these are compared**:

  ```
  Digital Signature Info of: FILE
  Signature #N:
    - Signature Field Name: ...
    - Signature Type: ...
    - Signed Ranges: [A - B], [C - D]
    The signature form field is not signed.
  File 'FILE' does not contain any signatures
  ```

  `Signature Type` is the `/SubFilter` name echoed back, `unknown` for a name the tool
  does not recognise; `Signed Ranges` is `/ByteRange` echoed back, as start and end
  offsets rather than the start-and-length pairs the file stores; the last line comes with
  exit 2. Everything else `pdfsig` prints is **excluded and must not be relied on**:
  `Signature Validation:`, `- Not total document signed`, `Signer Certificate Common
  Name`, `Signer full Distinguished Name`, `Signing Hash Algorithm`, `Signing Time` and
  every line on standard error. Neither image contains a certificate database, so
  validation fails identically for every signature, `Not total document signed` is printed
  even for a `/ByteRange` that covers the file exactly, and `Signing Time` is a formatted
  local date that the environment contract does not pin. **No case anywhere asks
  whether a signature is valid.**
* **product** (`encrypt`, `acroform`, `password`) — three options write a PDF file rather
  than a report: `--decrypt`, `--generate-appearances` and `--flatten-annotations`. The
  product is **never compared byte for byte**. Object numbering, object order, which
  objects are compressed and whether anything is compressed at all are all **free**. The
  spelling of the header version is **not**: `--decrypt` preserves the input's header
  version, and `info` compares `PDF version`, so a product that rewrites the header fails
  that comparison. What is compared is the exit status of the run that
  produced it, and then the answers the utilities give when they are run over the product,
  each by its own rule above — for `--decrypt`, that the text extracts and that
  `--is-encrypted` over the product exits 2; for the form options, that the value now
  appears in the page's **own** content stream through
  `qpdf --show-object=N --filtered-stream-data`, that the page dictionary no longer has an
  `/Annots` entry, and that `pdfinfo` reports `Form: none`.
* **warnings** (`codec`, `password`, `acroform`) — the exit status must match exactly, and
  so must the set of warnings. A warning's identity is derived like this: take the lines
  beginning `WARNING:`, drop that prefix, drop a following `FILE: `, `FILE (offset D): ` or
  `FILE (encryption dictionary, offset D): ` prefix, replace every run of decimal digits
  with `#`, collapse whitespace, and compare
  the results as a **set** — unordered, duplicates removed. The trailing line
  `qpdf: operation succeeded with warnings` is not part of the set; the exit status already
  carries it. This is the one comparison made over standard output and standard error
  **merged**, so that it does not depend on which of the two a warning arrives on. Where a
  damaged file is also expected to produce an answer, that answer is compared by the rule
  for it above. Poppler's own diagnostics are **not** compared under this rule or any
  other: the stderr a codec produces on a bad payload is the reference tool's prose and is
  outside the interface.

  **The identities this milestone's corpus actually turns on.** An implementation cannot guess a
  diagnostic's wording, and the set comparison above is over exact strings, so the vocabulary
  is stated here rather than left to be discovered. `FILE` stands for the operand as the tool
  was given it, and `#` for a run of decimal digits, both exactly as the normalization above
  produces them. Emitting one of these for a file that does not warrant it fails the case as
  surely as omitting it: the comparison is a set, not a checklist.

  ```
  FILE, object # # at offset #: document does not have updated appearance streams, so form fields will not be flattened
  FILE, stream object # #: unable to filter stream data
  ```

* **check** (`encrypt`, `password`) — `qpdf --check`. Wrapped lines are joined and
  whitespace runs collapsed before comparison, and only these lines are compared:

  ```
  checking FILE
  PDF Version: V
  File is linearized            (or: File is not linearized)
  No syntax or stream encoding errors found; the file may still contain errors
  that qpdf cannot detect
  ```

  `FILE` is the operand exactly as it was given on the command line. For an encrypted file
  `--check` also prints the encryption block, which is compared by the **encryption** rule
  above; each line of the report belongs to exactly one of the two rules. Lines outside
  both lists are ignored.

**A documented limit.** The products of the three writing options are read back by **your
own utilities**, because no other PDF implementation is present; the expectation they are
compared against was produced by running the same sequence end to end under the reference
tools. The expected text is fixed by the reference — a flattened field must yield the
reference's exact characters, not merely something self-consistent — and every fact a
`product` case rests on also appears read-only elsewhere, on a corpus file already in
that state.

**A documented silence.** `pdfimages` exits 0 on total codec failure, so a `codec` case is
never won by exiting non-zero and never lost by exiting zero. Two failure modes of the
same codec — a payload that is corrupt and the same payload truncated — produce the same
output file and the same status, and are distinguishable only by the reference tool's
prose on standard error, which is not compared. The corpus therefore contains at most one
case per distinguishable outcome, and a correct implementation is not asked to tell those
two apart.

**A documented refusal.** `--flatten-annotations` on a document whose `/NeedAppearances`
is true exits 3 and flattens nothing; the correct pipeline is
`--generate-appearances --flatten-annotations=all`, which exits 0. In the opposite case —
a field that has a value but no appearance stream and no `/NeedAppearances` — the flatten
**silently discards the value** at exit 0 with no warning, and the text that was
extractable before the flatten is gone after it. That outcome is expected exactly as it
stands, for both tools' behaviour is the same and it is what the standard's appearance
rules imply.

**Two statements of fact about the corpus.** No corpus file sets `/StrF` to `/Identity`:
the two reference tools disagree about such a file, one reporting the document as
unencrypted at exit 0 while the other decrypts it correctly, so it has no single
right answer. No corpus file carries a `/Version` entry in its catalogue, for the
reason the previous milestone gave. `/EncryptMetadata false` **does** appear, and is
compared only through the **object** rule: it has no signature whatever in `pdfinfo`,
`--show-encryption` or `--check`, and a case built on those would be unanswerable.

## 5. Checking progress

A sample corpus with its expected answers, built from the same reference tools, ships
in the image:

```
make && bash /app/tests-public/run-public-tests.sh            # THIS milestone's sample
bash /app/tests-public/run-public-tests.sh text robust       # named groups only
bash /opt/pdf-tests-public/run-public-tests.sh               # the first milestone's sample
```

Each covers only part of the milestone: passing one does not mean the milestone is met,
but a utility that fails it has not met the milestone either. `/app/tests-public/` ships with this
milestone; `/opt/pdf-tests-public/` is the first milestone's and is a regression check
on what you built before.

## 6. Normative documents in this image

* `/opt/pdfspec/ISO-32000-1-2008.txt` — the published ISO 32000-1:2008 text
  (PDF 1.7), the normative definition of the file format.
* `/opt/pdfspec/man-pdfinfo.txt`, `man-pdftotext.txt`, `man-pdffonts.txt`,
  `pdfimages` and `pdfsig` have **no man page in this image**. Their interfaces are fixed
  entirely by this document: the synopses in §2 and the comparison rules in §4.1.
* `/opt/pdfspec/qpdf-running-qpdf.txt` — the qpdf manual's command-line chapter,
  which defines `--show-object`, `--filtered-stream-data`, `--raw-stream-data`,
  `--check`, `--show-xref`, `--show-pages`, `--show-linearization`, `--show-encryption`,
  `--is-encrypted`, `--decrypt`, `--password`, `--generate-appearances`,
  `--flatten-annotations` and the exit status.
* `/app/SPEC/pdf16-17.md` — this milestone's requirements in the standard's terms, with
  `/app/SPEC/pdf15.md`, `/app/SPEC/pdf14.md` and `/app/SPEC/pdf13.md` for the previous
  milestones', all of which still apply.
