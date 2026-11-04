# TEST_INTERFACE.md — how this repository is built and driven, and how its output is compared

This file is authoritative for the mechanics of this workspace: the build command, the
programs and option spellings in use, the one environment hook, and exactly how
each output is compared with the reference.

## 1. Build

`make`, run in `/app`, builds every utility into `/app/build/` under its real program name.
That exact command — plain `make`, no added flags, with `/app` as the working directory — is
how the utilities are built:

```
make            # -> /app/build/pdfinfo, /app/build/pdftotext, /app/build/pdffonts,
                #    /app/build/qpdf, /app/build/pdfimages, /app/build/pdfsig
make clean
```

**No program is new at this milestone.** These six are the previous milestone's six, at the
same paths, built by the same `make`, and all six must still work on every document: a
milestone spent on AES-256 does not excuse a `pdfimages` or a `pdfsig` that stopped building.

The utilities are invoked with `/app` as their working directory, and are given a `PATH`
containing only `/app/build`, so they must not depend on any other program being reachable by
name. Each utility is self-contained: it starts no other program, opens no network
connection, and loads no code beyond its own binary and the C runtime. They are also run with `LC_ALL=C.UTF-8`. Do not depend on the ambient locale for case
folding, collation, or number formatting; this one is pinned, and output is compared as bytes.

The workspace must not contain the *sources* of third-party PDF engines — MuPDF, pdfium,
poppler, qpdf and HarfBuzz, each recognized by its own public symbol names. This rule is about
vendoring someone else's engine, not a restriction on your naming: it matches whole identifiers,
ignores comments, and covers no name a library written from scratch would arrive at on its own.
The rule reaches this milestone's cryptography exactly as it reached the previous one's: write
your own AES-256 and your own SHA-256, SHA-384 and SHA-512, or take them from a general-purpose
library you may already link, but do not import another PDF engine's.

The **built binaries** are covered as well, separately from the sources: in
`/app/build/`'s utilities, `strings` must find none of these vendor markers
(`MuPDF`, `Artifex`, `Ghostscript`, `poppler`, `pdfium`, `libqpdf`, `qpdf version`, `xpdf`,
`HarfBuzz`, `FreeType`), and no symbol `readelf` lists may carry one of these
third-party prefixes (`fz_`, `FPDF`, `opj_`, `jbig2_`, `hb_`). Unlike the source rule, the
marker rule applies **anywhere in the binary**, including data sections — because a
`Makefile` recipe could fetch a prebuilt engine and install it as `/app/build/pdfinfo`, which
would satisfy every other rule. None of those markers is a name a library written from
scratch produces on its own.


The implementation language is **C** (C11). The image provides `gcc`, `make`, `binutils`,
`gdb` and `valgrind`, plus `zlib`.

The utilities must be compiled ELF binaries produced by that `make`, and each one's
dynamic dependencies must lie within the C runtime and zlib:

```
libc.so  libm.so  libz.so  libgcc_s.so  libpthread.so  libdl.so  ld-linux
```

No utility may be a script, link anything outside that list, or be byte-identical to another
binary shipped in the workspace. Static linking is fine. Each utility must be a regular file: a
symbolic link in a utility's place is removed before the utilities are run, and that utility
counts as not built. The workspace must build from source with `make` in a container that has a
C toolchain and **no PDF implementation of any kind** — no poppler, no qpdf, no mupdf, no
pdfium, no PDF module for python — and no independent cryptographic tool either. A utility that
calls out to a system PDF tool, or to a system program to perform a hash or a cipher, finds
none to call.

Leave `/app` source-only. The workspace is archived and restored when it is handed on, so
delete build products outside `build/` before you finish, and compile scratch
programs under `/tmp`.

## 2. The utilities and their options

The programs are **not** this task's invention. Each is an existing published command-line
interface adopted as-is, with the same program name, the same option letters, the same operand
order and the same output layout: `pdfinfo`, `pdftotext`, `pdffonts`, `pdfimages` and `pdfsig`
are **poppler-utils**; `qpdf` is the **qpdf** command-line tool, as the qpdf manual defines it.

The earlier milestones' options are **carried forward unchanged**, and are still exercised;
nothing here narrows them. The block below names the options this milestone's own
cases use, and the one option that is new:

```
pdfinfo   [-f N] [-l N] [-opw PW] [-upw PW] FILE
pdftotext [-f N] [-l N] [-enc ENC] [-opw PW] [-upw PW] FILE [OUTFILE|-]
pdffonts  [-f N] [-l N] [-opw PW] [-upw PW] FILE
qpdf [--password=PW] --show-encryption [--show-encryption-key] FILE
qpdf [--password=PW] --check [--show-encryption-key] FILE
qpdf [--password=PW] --is-encrypted FILE
qpdf [--password=PW] --show-object={trailer|N[,G]} [--filtered-stream-data|--raw-stream-data] FILE
qpdf [--password=PW] --decrypt FILE {OUTFILE|-}
```

**`--show-encryption-key` is the only addition to the interface at this milestone.** It is a
modifier, not an inspection option: it is given together with `--show-encryption` or with
`--check`, never alone, and it adds **exactly one line** to the report that option already
produces. Exactly one inspection option is still given per invocation, and the inspection
options are never combined with each other.

`--password=PW` may accompany any `qpdf` invocation, as before, and is the qpdf spelling of
what `-upw`/`-opw` do for the poppler programs: one option, tried as both passwords. Page
numbers on the command line are 1-based. `-` as an output operand writes to standard output.
Diagnostics go to standard error, never to standard output. The only text encoding exercised is
`-enc UTF-8`, which is also `pdftotext`'s default; no other `ENC` value is used, and §4.2
says why no `ENC` value can rescue a string the reference tool decodes wrongly.

**Exit status.** The previous milestones' rules all still hold. Four things are new here, and
all four are required:

* **`qpdf --check` exits 3** — the operation succeeded, with warnings — when the document's
  `/Perms` block decrypts but does not agree with `/P` or with `/EncryptMetadata`. The report
  is printed in full on standard output and the warning is on standard error;
* **`qpdf --check` exits 2**, printing **nothing at all** on standard output, when a `/V 5`
  encryption dictionary is missing `/UE` or `/Perms`. The distinction between these two is the
  heart of the `perms` group: a block that is *wrong* is a warning, a parameter set that is
  *incomplete* is an error;
* **a wrong password and a wrong password hash are the same answer.** A document whose `/U`,
  `/UE`, `/O` and `/OE` were computed by the wrong algorithm — `/R 5`'s hash under `/R 6`, or
  the reverse — is rejected exactly as a wrong password is: **exit 1** from the poppler
  programs with `Incorrect password`, **exit 2** from `qpdf` with `invalid password`. There is
  no third outcome and no partial read;
* **`qpdf --is-encrypted` still exits 0 for an encrypted file and 2 for one that is not**, and
  the 2 is also what a malformed `/V 5` dictionary produces. A file whose `/U` is corrupt is
  still **0** — it is encrypted, whoever can open it. This is why `--is-encrypted` is never the
  sole evidence for anything in §4.1.

**A file that does not exist is not a silent case.** Measured, for every utility here: the
poppler programs exit **1** and `qpdf` exits **2**, each writing a diagnostic to standard error
and nothing to standard output. No case can be passed by failing to open a file.

## 3. Environment hooks

There is exactly one:

* **`SOURCE_DATE_EPOCH`** (the reproducible-builds.org specification) — when set, any timestamp
  a utility writes into a file it produces uses that instant instead of the wall clock. It
  is always set when the utilities run. Among this milestone's cases, the only file an
  invocation produces is the one `--decrypt` writes.

  The rest of the environment the utilities run in is pinned as well -- `PATH` to the
  build directory, `LC_ALL=C.UTF-8`, and `HOME` and `TMPDIR` to `/tmp` -- so that a utility
  writing a temporary file behaves exactly as it does under your own
  `make && run-public-tests.sh`. None of those four selects behaviour; the hook above is
  the only thing in this workspace that does.

Input and output paths are the operands the interfaces above already define. There is no mode
flag, no profile, no compatibility switch, no version banner and no marker string in this
workspace; behaviour is selected by the options above and by the content of the PDF file.

## 4. What the output must match

The utilities are run over a corpus of PDF files, and their output must match answers
recorded from real independent implementations. The cases fall into these groups:

| group | what one case is |
| --- | --- |
| `aes256` | one command over one AES-256 document that opens with the empty password, whose answer is what a report *says about* the encryption: the `Encrypted:` field of `pdfinfo`, one `qpdf --show-encryption` block with or without `--show-encryption-key`, the encryption block of one `--check`, one `--is-encrypted` status, or one `--show-object` of the encryption dictionary |
| `decrypt` | one command over one AES-256 document whose answer is what came *out of* the decryption: a page of `pdftotext`, the fonts of `pdffonts`, one `--show-object` of a string, one `--filtered-stream-data` of a stream, or the file `--decrypt` writes and what the utilities then report of it |
| `perms` | one command over one document whose `/Perms` block, or whose `/V 5` parameter set, does not agree with itself: the exit status, the set of warnings, and the report where one is printed |
| `password` | one command over one document that does not open with the empty password, or whose password hashes were computed by the wrong algorithm: the exit status, and the answer where the right password is supplied |
| `version` | one command over one document whose declared version is the answer: the `PDF version:` line of `pdfinfo`, or the `PDF Version:` line of `qpdf --check`, with the extension level where the file carries one |

Every case belongs to exactly one group. The group is a property of the corpus file, not of the
command, so the same `pdftotext` page is counted under `decrypt` for a file the corpus pairs
with an unencrypted twin, and under `password` for a file that needs a password to open; it is
never counted twice. A file that could sit in two families belongs to whichever the corpus
assigns it, and the corpus assigns each file exactly one.

The corpus also contains files within the previous milestones' scope, compared by those
milestones' rules: what worked there must keep working.

### 4.1 Exactly how outputs are compared

Unless a rule below says otherwise, the compared text is the utility's **standard output
only**, and the exit status must match as well.

**Expectations are generated, never written by hand.** Every expectation is recorded by running
the reference tools — poppler-utils 22.12.0 and **qpdf 11.3.0** — over the corpus file. This
matters most for `--show-object`, whose output is not a transcription of the file's bytes: it
re-sorts keys into ASCII order, deletes a key whose value is null, and deletes a key whose
indirect reference does not resolve, all silently and at exit 0. It matters again for every
report whose exact wording, column layout and line order are the reference tool's own. A
hand-written expectation would disagree with the reference on files that are perfectly correct,
so none is used.

**A line that was never produced is not a difference.** Every rule below that compares lines
also compares **how many lines were produced**, against the count the file determines. This is
not a formality at this milestone: the `--show-encryption` block is **17 lines** for a document
whose `/V` is 4 or greater and **14 lines** for one whose `/V` is 2, and the difference is
three lines that a correct implementation must *emit* and an incorrect one silently omits. A
comparison that only checked the lines that were produced would accept the shorter report as a
perfect prefix of the longer one.

* **text** (`decrypt`, `password`) — compared as a *character sequence*, not as a layout, under
  the normalization the previous milestones define: Unicode NFC, soft hyphen `U+00AD` removed,
  control characters replaced by a space, every run of whitespace collapsed to one space,
  leading and trailing whitespace stripped. For an AES-256 document this is the whole of the
  decryption signal: the corpus pairs a document with its unencrypted twin, the page text is
  the same on both sides, and a wrong file key produces nothing or rubbish rather than a
  diagnostic. There is no middle outcome at `/V 5` — the key is right and the page is the twin's
  page, or the key is wrong and there is no page.
* **info** (`aes256`, `version`) — the same fields the previous milestones compare, as
  `Key: value` lines in this order:

  ```
  Title, Author, Creator, Producer, CreationDate, ModDate, Custom Metadata,
  Metadata Stream, Tagged, UserProperties, Suspects, Form, JavaScript, Pages,
  Encrypted, Page size, Page rot, File size, Optimized, PDF version
  ```

  Each value has internal whitespace runs collapsed, and a field the report does not carry is
  absent from both sides. Lines outside that list are ignored, so emitting them is
  harmless. Two of those fields are where this milestone's work shows:
  * `Encrypted` is `no`, or `yes` followed by a parenthesised list —
    `yes (print:yes copy:yes change:yes addNotes:yes algorithm:AES-256)` — in which each of
    the four permission words tracks the corresponding `/P` bit and `algorithm:` names the
    cipher. **`AES-256` is a
    new value of that field**, distinct from the `AES` a `/V 4` `/AESV2` document prints and
    from `RC4`, and it is the only place poppler names this milestone's cipher at all. `/R 5`
    and `/R 6` print it identically — the revision is not visible here, only through the
    `encryption` rule below;
  * `PDF version` is the document's version under `/app/SPEC/pdf20.md` §6.2: the header's,
    unless the catalogue's `/Version` is later, in which case the catalogue's. A catalogue
    entry *earlier* than the header is ignored, and reporting it would be wrong.

  `Form` has a third value, `XFA`, which a document whose interactive form dictionary carries
  an `/XFA` entry produces in place of `AcroForm`. It is the previous milestone's field and any
  such file falls within that milestone's scope; it is named here because the previous milestone never produced it.
* **encryption** (`aes256`, `perms`, `password`) — `qpdf --show-encryption`, and the same block
  where `--check` prints it inline. The block's wording, its line order and its nine permission
  lines are the previous milestone's, unchanged, and are compared the same way: in order, with
  whitespace runs collapsed. The block is:

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
  stream encryption method: AESv3|AESv2|RC4|none
  string encryption method: ...
  file encryption method: ...
  ```

  The nine permission lines are derived from `/P` and `/R` together, not from `/P` alone.
  Four things are new, and all four are compared:
  1. **`R = 5` or `R = 6`.** This is the only place in either tool that the revision appears;
     `pdfinfo` says `AES-256` for both.
  2. **The three `encryption method:` lines name `AESv3`** for `/AESV3`, exactly as they name
     `AESv2` for `/AESV2`.
  3. **Those three lines are emitted whenever `/V` is 4 or greater.** The previous milestone's
   TEST_INTERFACE states the same rule, and this milestone is where `/V 5` first
   exercises it.
     Measured: `/V 5` prints them naming `AESv3`, `/V 4` prints them naming `AESv2`, and `/V 2`
     prints none at all. Their absence is as much a part of the answer as their content, and it
     is the line-count rule above that enforces it.
  4. **`--show-encryption-key` inserts exactly one line**, `Encryption key = ` followed by the
     file encryption key as lower-case hexadecimal — 64 characters for AES-256 — **immediately
     after the `User password = ` line**, in `--show-encryption` and in `--check` alike. The
     key is a deterministic function of the document and the password supplied, so this one
     line is a direct comparison of Algorithm 2.A and Algorithm 2.B against the reference.

  **`--show-encryption` is not a correctness test, and no case treats it as one.** It exits
  **0** even on a document that no tool can open: for a wrong password, and for a document
  whose password hashes were computed by the wrong algorithm, it prints `Incorrect password
  supplied` and then prints `R`, `P` and the permission lines anyway. It is **3**, not 0,
  when qpdf issues a warning about the encryption dictionary itself -- a `/Perms` block that
  disagrees with the parameters around it, for instance -- and six cases turn on that
  status. The rule is about AUTHENTICATION, not about warnings: failing to open a document
  never changes this status, and a warning always does. Those are read out of the
  dictionary without authenticating anything. Where a case turns on whether a document opens,
  the answer is the status of `--check`, `pdfinfo` or `pdftotext`, never this one.
* **object** (all five groups) — `qpdf --show-object`, compared with whitespace runs collapsed
  to one space and leading and trailing whitespace stripped. The previous milestones' properties
  all still hold — ASCII key re-sorting, the two silent deletions, reals passed through
  verbatim, escapes and empty containers preserved, strings and stream data coming back
  decrypted — and two are added here:
  1. the `/V 5` **encryption dictionary is reported as it stands**, its own strings never having
     been encrypted, and it is the only place `/UE`, `/OE`, `/Perms`, `/CF` and
     `/EncryptMetadata` are visible at all. `/O` and `/U` print as 48-byte hex strings, `/UE`
     and `/OE` as 32-byte ones and `/Perms` as a 16-byte one;
  2. a string whose bytes are not printable ASCII prints as a **hex string**, and the form it
     was written in is not preserved: a literal string and a hex string holding the same bytes
     print identically. The bytes themselves survive exactly, which is what makes this the one
     rule under which a text string's encoding is compared at all (§4.2).
* **check** (`aes256`, `perms`, `version`) — `qpdf --check`. Wrapped lines are joined and
  whitespace runs collapsed, and the same lines the previous milestone compared are compared
  here — only these:

  ```
  checking FILE
  PDF Version: V
  File is linearized            (or: File is not linearized)
  No syntax or stream encoding errors found; the file may still contain errors
  that qpdf cannot detect
  ```

  `FILE` is the operand exactly as it was given on the command line, and `PDF Version:`
  carries this milestone's version answer — plainly `2.0` for a 2.0 header, and `2.0
  extension level N` where the catalogue's extensions dictionary gives a developer
  extension level. `qpdf` reports the header's version and **ignores the catalogue's
  `/Version` entirely**, where `pdfinfo` honours a later one; the two tools disagree by
  design and each is compared against itself. For an encrypted document `--check` also
  prints the encryption block, compared by the **encryption** rule above; each line of the
  report belongs to exactly one of the two rules, and lines outside both lists are
  ignored.
* **warnings** (`perms`) — the exit status must match exactly, and so must the set of warnings.
  A warning's identity is derived as the previous milestone defines it: take the lines beginning
  `WARNING:`, drop that prefix, drop a following `FILE: `, `FILE (offset D): ` or
  **`FILE (DESCRIPTION, offset D): `** prefix — the third shape is new here, because the
  warning this milestone compares names the object it came from — replace every run of decimal
  digits with `#`, collapse whitespace, and compare the results as a **set**, unordered and
  duplicate-free. The trailing line `qpdf: operation succeeded with warnings` is not part of the
  set; the exit status already carries it. This is the one comparison made over standard output
  and standard error **merged**. The single warning the `perms` group turns on normalizes to:

  ```
  /Perms field in encryption dictionary doesn't match expected value
  ```

  It is produced identically for a `/Perms` that encodes the wrong `/P`, for one whose
  ciphertext has been corrupted, and for one whose metadata byte disagrees with
  `/EncryptMetadata`. Those three are one answer, not three, and the corpus does not ask an
  implementation to tell them apart.
* **product** (`decrypt`) — `qpdf --decrypt` writes a PDF file rather than a report. The product
  is **never compared byte for byte**: object numbering, object order and what is compressed are
  all free. What is compared is the exit status of the run that produced it, and then the
  answers the utilities give when run over the product — that the text extracts, that
  `--is-encrypted` over the product exits 2, and that `--show-object` of the trailer has no
  `/Encrypt`. **The declared version is not free.** The reference preserves it: a 2.0 document
  decrypts to a 2.0 document, and `pdfinfo` over the product reports `PDF version: 2.0`. A
  product whose header has been rewritten to another version is a different answer.
* **status** (`password`, `perms`) — where a document does not open, the answer is the exit
  status alone and nothing is compared on standard output. Poppler exits **1** and `qpdf` exits
  **2**, and both tools are run over the same file, because the pair of statuses is
  the answer. Supplying a password to a document that is not encrypted is ignored entirely and
  leaves the status 0, so passing one is not by itself evidence of anything.

**A documented limit.** The product of `--decrypt` is read back by **your own utilities**,
because no other PDF implementation is present; the expectation it is compared against was
produced by running the same sequence end to end under the reference tools. The expected
text is the reference's, and every fact a `product` case rests on also appears read-only
elsewhere, on a corpus file already in that state.

**Three statements of fact about the corpus.** No corpus file sets `/StrF` to `/Identity` at
`/V 5`: measured, poppler reports such a file's algorithm as `RC4` and returns no page text at
exit 0 while qpdf handles it correctly, so it has no single right answer. No
corpus file carries a truncated `/U`, for the same reason — the two tools disagree about it,
one reporting the document unencrypted and the other returning decrypted-looking rubbish at
exit 3. One divergence *is* in the corpus and is fenced instead of excluded: a `/V 5` document
missing `/UE` is compared **through `qpdf` only**, where it is the exit-2 error of the `perms`
group; poppler reports `Encrypted: no` at exit 0 for it, and that answer is not compared for any
tool.

### 4.2 Required, and not visible — and the measurement that says why

Three of the constructs `/app/SPEC/pdf20.md` requires **cannot be observed through these
interfaces**, and one more can be observed only in part. They are required all the same: a
document carrying any of them may appear in any group's corpus file, and every answer over
that file must be unchanged by its presence — which is a real obligation, because a reader
that trips over a structure it does not understand fails the ordinary cases too, and an
implementation that drops an unfamiliar key fails the **object** rule over the same file.
What is not observable is the construct *itself*. Each was measured, and the measurement is
given below.

1. **UTF-8 text strings, through the poppler programs.** poppler 22.12.0 does not implement
   them. A correct `EF BB BF` `/Title` is printed as mojibake — every byte read as
   PDFDocEncoding and re-encoded — at **exit 0 with empty stderr**, at a 1.7 header and a 2.0
   header alike, for every field of the document information dictionary, for an outline title
   and for an annotation's contents. **No `-enc` value fixes it**, `UCS-2` merely producing
   empty output. Comparing that field would require an implementation to reproduce the reference's
   defect in order to match, so **no corpus file carries a UTF-8 text string in a compared
   report field**. What *is* compared is the bytes: under the **object** rule a text string round-trips
   as hexadecimal, byte for byte, and that comparison is exact.
2. **`/AF` as a discovery mechanism.** No tool in the pinned set implements it. Every
   attachment-aware view walks only `/Names /EmbeddedFiles`, so a document whose only
   attachment is reached through `/AF` produces output byte-identical to a document with no
   attachment at all. No attachment-aware command is even licensed in §2. The `/AF` array, the
   file specification and `/AFRelationship` are compared only as serialisation, under the
   **object** rule, on whatever group's file carries them.
3. **Where an output intent sits.** A page-level intent, a catalogue-level intent and no intent
   at all are **identical** under `pdfinfo`, `pdftotext`, `pdffonts` and `qpdf --check`, with
   matched headers and matched page text. Neither tool has a notion of an output intent, and
   neither validates one: an intent with no `/DestOutputProfile` at all passes `--check` at
   exit 0 with zero warnings, exactly like a complete one. Again the dictionary is compared as
   serialisation and the placement is not compared at all.
4. **Deprecation.** Neither tool has any notion of a construct being deprecated at a version.
   Five constructs were each built twice with **byte-identical bodies**, differing only in the
   8-byte header, and every one produced identical output from every command at identical exit
   codes: XFA in both its forms, `/Movie` and `/Sound` annotations, the catalogue's `/Perms`
   with `/UR3`, and a clean control. RC4 and AES-128 inside a `%PDF-2.0` file pass `--check` at
   exit 0 with zero warnings. **A deprecation is therefore never the answer to a case**,
   and an implementation that refused, warned about or annotated such a file would get those
   cases wrong.

## 5. Checking progress

A sample corpus with its expected answers, built from the same reference tools, ships
in the image:

```
make && bash /opt/pdf-tests-public/run-public-tests.sh
bash /opt/pdf-tests-public/run-public-tests.sh text robust   # named groups only
```

It covers only part of the milestone: passing it does not mean the milestone is met, but a
utility that fails it has not met the milestone either.

## 6. Normative documents in this image

* `/app/SPEC/pdf20.md` — this milestone's requirements. **ISO 32000-2 is not shipped in this
  image**, so that file is the whole of the normative text for what PDF 2.0 adds.
* `/opt/pdfspec/ISO-32000-1-2008.txt` — the published ISO 32000-1:2008 text, the normative
  definition of the underlying file format.
* `/opt/pdfspec/man-pdfinfo.txt`, `man-pdftotext.txt`, `man-pdffonts.txt` — the poppler-utils
  man pages that define those three interfaces. There is no man page in this image for
  `pdfimages` or `pdfsig`; §2 and the previous milestone's rules define them.
* `/opt/pdfspec/man-qpdf.txt` and `/opt/pdfspec/qpdf-running-qpdf.txt` — qpdf's man page and
  the manual's command-line chapter, which defines `--show-object`, `--filtered-stream-data`,
  `--raw-stream-data`, `--check`, `--show-encryption`, `--show-encryption-key`,
  `--is-encrypted`, `--decrypt`, `--password` and the exit status. The manual defines the
  options, not the wording of their reports: the lines this milestone compares are the
  ones §4.1 above states.
* `/app/SPEC/pdf16-17.md`, `pdf15.md`, `pdf14.md`, `pdf13.md` — the previous milestones'
  requirements, all of which still apply.
