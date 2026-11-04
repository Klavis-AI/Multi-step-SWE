# TEST_INTERFACE.md — how this repository is built and driven, and how its output is compared

This file is authoritative for the mechanics of this workspace: the build command, the
programs and option spellings in use, the one environment hook, and exactly
how each output is compared with the reference.

## 1. Build

`make`, run in `/app`, builds every utility into `/app/build/` under its real program
name. That exact command — plain `make`, no added flags, with `/app` as the working
directory — is how the utilities are built:

```
make            # -> /app/build/pdfinfo, /app/build/pdftotext, ...
make clean
```

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
`fz_open_document()` is not.

The **built binaries** are covered as well, separately from the sources: in
`/app/build/`'s four utilities, `strings` must find none of these vendor markers
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
qpdf, no mupdf, no ghostscript, no pdfium, no PDF module for python. A utility that calls
out to a system PDF tool therefore finds none to call.

Leave `/app` source-only. The workspace is archived and restored when it is
handed on, so delete build products outside `build/` before you
finish, and compile scratch programs under `/tmp`.

## 2. The utilities and their options

The programs are **not** this task's invention. Each is an existing published
command-line interface adopted as-is, with the same program name, the same option
letters, the same operand order and the same output layout:

* `pdfinfo`, `pdftotext`, `pdffonts` — **poppler-utils**, as defined by the
  `pdfinfo(1)`, `pdftotext(1)` and `pdffonts(1)` man pages.
* `qpdf` — the **qpdf** command-line tool, as defined by the qpdf manual
  ("Running qpdf").

Only the options listed below are exercised. Behaviour for all of them is the
behaviour those documents describe.

```
pdfinfo   [-f N] [-l N] FILE
pdftotext [-f N] [-l N] [-enc ENC] FILE [OUTFILE|-]
pdffonts  [-f N] [-l N] FILE
qpdf --show-object={trailer|N[,G]} [--filtered-stream-data | --raw-stream-data] FILE
qpdf --check FILE
qpdf --show-xref FILE
qpdf --show-pages FILE
qpdf --show-linearization FILE
```

The four qpdf lines after `--show-object` are new at this milestone. Exactly one qpdf
inspection option is given per invocation; they are never combined with each other,
and none of them takes an argument.

Page numbers on the command line are 1-based. `-` as `pdftotext`'s output operand
writes to standard output. Diagnostics go to standard error, never to standard
output. The only text encoding exercised is `-enc UTF-8`, which is also
`pdftotext`'s default; no other `ENC` value is used.

**Exit status.** For `pdfinfo`, `pdftotext` and `pdffonts` it is compared exactly, not
merely as zero versus non-zero: **0** on success, and poppler's own value when it fails
-- **1** when the PDF could not be opened, **2** when the output file could not be. For `qpdf` the manual's three values are
all exercised: **0** when nothing was found wrong, **3** when the
operation succeeded but warnings were issued, **2** when errors were found. A file
with a cross-reference chain qpdf must reconstruct, and a file whose linearization
data is internally inconsistent, both exit **3** — that status is the signal,
so a utility that reports the same damage with exit 0 fails those cases.

## 3. Environment hooks

There is exactly one:

* **`SOURCE_DATE_EPOCH`** (the reproducible-builds.org specification) — when set, any
  timestamp a utility writes into a file it produces uses that instant instead of the
  wall clock. It is always set when the utilities run. The rest of their environment is
  pinned as well -- `PATH` to the build directory, `LC_ALL=C.UTF-8`, and `HOME` and
  `TMPDIR` to `/tmp` -- so that a utility writing a temporary file behaves exactly as it
  does under your own `make && run-public-tests.sh`. None of those four selects behaviour; the
  hook above is the only thing in this workspace that does.

Input and output paths are the operands the interfaces above already define. There is
no mode flag, no profile, no compatibility switch, no version banner and no marker
string in this workspace; behaviour is selected by the options above and by the
content of the PDF file.

## 4. What the output must match

The utilities are run over a corpus of PDF files, and their output must match answers
recorded from real independent implementations. The cases fall into these groups:

| group | what one case is |
| --- | --- |
| `update` | one command over one incrementally updated file: a page of `pdftotext`, the compared field subset of `pdfinfo`, or one `qpdf --show-pages` listing |
| `transparency` | one object of `qpdf --show-object=N FILE` |
| `revision` | one `qpdf --show-xref` listing, or one `qpdf --show-object=trailer` |
| `linearization` | one command over one file that does or does not claim linearization: the compared field subset of `pdfinfo`, the `qpdf --check` report, one `qpdf --show-linearization` dump, or the SHA-256 of one object's stream data |
| `damaged` | one file whose cross-reference chain or linearization data is broken: the exit status and the warnings that go with it, or the SHA-256 of one object's stream data |

Every case belongs to exactly one group. The group is a property of the corpus file,
not of the command, so the same field of `pdfinfo` is counted under `update` for a file
built by incremental update and under `linearization` for a file that claims to be
linearized; it is never counted twice.

The corpus also contains files within the previous milestone's scope, compared by that
milestone's rules: what worked there must keep working.

### 4.1 Exactly how outputs are compared

Unless a rule below says otherwise, the compared text is the utility's **standard
output only**, and the exit status must match as well.

**Expectations are generated, never written by hand.** Every expectation in this
milestone is recorded by running the reference tools — poppler-utils 22.12.0 and
**qpdf 11.3.0** — over the corpus file. This matters most for `--show-object`, whose
output is not a transcription of the file's bytes: see the four normalizations under
**transparency** below. A hand-written expectation will disagree with the reference on
files that are perfectly correct, so none is used.

* **text** (`update`) — compared as a *character sequence*, not as a layout. Both sides
  are normalized: Unicode NFC, soft hyphen `U+00AD` removed, control characters
  replaced by a space, every run of whitespace collapsed to one space, leading and
  trailing whitespace stripped. Where the line breaks and how many spaces fall between
  words therefore do not matter; which characters the page contains, and in what order,
  does.
* **info** (`update`, `linearization`) — only the fields below are compared, as
  `Key: value` lines in this order, each value with internal whitespace runs collapsed.
  A field the report does not carry for a given file is simply absent from both sides:

  ```
  Title, Author, Creator, Producer, CreationDate, ModDate, Custom Metadata,
  Metadata Stream, Tagged, UserProperties, Suspects, Form, JavaScript, Pages,
  Encrypted, Page size, Page rot, File size, Optimized, PDF version
  ```

  `Subject` and `Keywords` are not compared. Lines outside this list are ignored, so
  emitting them is harmless. Two of these fields are where this milestone's work shows:
  `Pages` and `Page size` after an update has changed the page tree or a `/MediaBox`,
  and `Optimized` for a file that claims linearization.
* **pages** (`update`) — `qpdf --show-pages`. Each line is stripped of leading and
  trailing whitespace and its internal whitespace runs collapsed to one space; empty
  lines are dropped; the resulting line sequence is compared in order. It is the
  sequence `page N: n g R`, `content:`, then one `n g R` line per content stream. A
  page removed by an update is absent from it even though the page's objects remain in
  the file and remain resolvable — the page tree is what is compared, not the presence
  of objects.
* **object** (`transparency`, `revision`) — `qpdf --show-object`. Compared with
  whitespace runs collapsed to one space and leading and trailing whitespace stripped,
  so the line wrap and the two spaces in the stream header `Object is stream.
  Dictionary:` do not matter. Four properties of the reference output are deliberate
  and are what the comparison tests:
  1. keys are re-sorted into ASCII order, so uppercase keys precede lowercase ones and
     `/ca` follows `/Type`; the authoring order in the file is not recoverable;
  2. a key whose value is null is **dropped**, and so is a key whose indirect reference
     does not resolve — a soft mask's dangling `/G` simply vanishes from the reported
     dictionary, with no diagnostic and exit 0. The disappearance is the signal;
  3. reals are passed through **verbatim**, not renormalized: `.25` stays `.25` and
     `1.0` stays `1.0`, so the spelling of a number in the file is part of the answer;
  4. `#xx` escapes in names and `\)` escapes in strings survive; indirect references
     print as `n g R`; empty `<< >>` and `[ ]` are preserved.
* **xref** (`revision`) — `qpdf --show-xref`. Lines of the form `n/g: uncompressed;
  offset = D`, compared in order with whitespace runs collapsed. The offsets are byte
  offsets into the corpus file, so they are fixed by the file rather than chosen by the
  implementation, and the object that matters is the one whose offset is the newest
  revision's. Only files with an intact cross-reference chain produce cases in this
  group: where a file must be reconstructed, the offsets are an artefact of the
  reference tool's own recovery and are not compared.
* **check** (`linearization`, `damaged`) — `qpdf --check`. Wrapped lines are joined and
  whitespace runs collapsed before comparison, and only these lines are compared:

  ```
  checking FILE
  PDF Version: V
  File is linearized            (or: File is not linearized)
  No syntax or stream encoding errors found; the file may still contain errors
  that qpdf cannot detect
  ```

  `FILE` is the operand exactly as it was given on the command line. Lines outside this
  list are ignored.
* **linearization dump** (`linearization`) — `qpdf --show-linearization`. Compared as
  `key: value` lines with runs of whitespace collapsed and ends stripped. **The
  per-page entries of the Page Offsets Hint Table are NOT compared** and are dropped
  from both sides before comparison: their `length`, `content_length`,
  `nshared_objects` and `identifier` fields cannot be derived from the file by any
  field model in ISO 32000-1 Table F.3 — established by exhaustive search over both
  bit orders, all field orderings and every plausible width — so comparing them would
  require reproducing a value nobody can compute from the standard. Everything else
  in the dump IS compared: all eight linearization-data fields, all thirteen Page
  Offsets Hint Table *parameters*, and the entire Shared Objects Hint Table.
* **stream data** (`damaged`, `linearization`) — `qpdf --show-object=N`
  `--filtered-stream-data` or `--raw-stream-data`. The comparison is the **SHA-256 of the
  bytes written to standard output**, so the payload is compared exactly and nothing about
  its length or encoding is reported back to you. `--filtered-stream-data` decodes the
  stream through its `/Filter` chain; `--raw-stream-data` writes the stored bytes
  untouched. A stream carrying no filter produces the same digest either way.
* **warnings** (`damaged`) — the exit status must match exactly, and so must the set of
  warnings. A warning's identity is derived like this: take the lines beginning
  `WARNING:`, drop that prefix, drop a following `FILE: ` or `FILE (offset D): `
  prefix, replace every run of decimal digits with `#`, collapse whitespace, and
  compare the results as a **set** — unordered, duplicates removed. The trailing line
  `qpdf: operation succeeded with warnings` is not part of the set; the exit status
  already carries it. This is the one comparison made over standard output and standard
  error **merged**, so that it does not depend on which of the two a warning arrives
  on. Where a damaged file is also expected to produce an answer, that answer is
  compared by the rule for it above.

  **The identities this milestone's corpus actually turns on.** An implementation cannot
  guess a diagnostic's wording, and the set comparison above is over exact strings, so
  the vocabulary is stated here rather than left to be discovered. `FILE` stands for the
  operand as the tool was given it, and `#` for a run of decimal digits, both exactly as
  the normalization above produces them. Emitting one of these for a file that does not
  warrant it fails the case as surely as omitting it: the comparison is a set, not a
  checklist.

  ```
  Attempting to reconstruct cross-reference table
  can't find startxref
  end of first page section (/E) mismatch: /E = #; computed = #..#
  error encountered while checking linearization data: FILE (linearization hint stream, offset #): expected n n obj
  error encountered while checking linearization data: FILE (linearization hint table, offset #): /N does not match number of pages
  file is damaged
  first page object (/O) mismatch
  xref not found
  ```


**A documented interaction.** An update that shrinks a page's `/MediaBox` has two
independent observable effects on the same file: `pdfinfo` reports the new `Page size`,
and `pdftotext` stops emitting the text that now falls outside the box. Both are
compared, as separate cases, and a reader that applies the update to only one of the two
paths fails one of them.

**A documented disagreement.** For a file that carries a linearization parameter
dictionary whose `/L` is correct but whose hint data is not, the two reference tools
disagree, and both answers are expected as they stand: `pdfinfo` reports
`Optimized: yes`, because poppler looks for the dictionary and does not validate it,
while `qpdf --check` prints `File is linearized`, warns about the hint data, and exits
3. Reproducing each interface's own behaviour is the requirement; there is no single
underlying verdict to which both must be reconciled.

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
* `/opt/pdfspec/man-pdfinfo.txt`, `man-pdftotext.txt`, `man-pdffonts.txt` — the
  poppler-utils man pages that define those three interfaces.
* `/opt/pdfspec/qpdf-running-qpdf.txt` — the qpdf manual's command-line chapter,
  which defines `--show-object`, `--filtered-stream-data`, `--raw-stream-data`,
  `--check`, `--show-xref`, `--show-pages`, `--show-linearization` and the exit
  status.
* `/app/SPEC/` — this milestone's requirements, and the previous milestone's, in the
  standard's terms.
