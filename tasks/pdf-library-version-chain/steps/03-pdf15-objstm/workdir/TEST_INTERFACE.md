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
behaviour those documents describe. **This milestone adds none**; the set is the
previous milestone's exactly:

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

Exactly one qpdf inspection option is given per invocation; they are never combined
with each other, and none of them takes an argument.

Page numbers on the command line are 1-based. `-` as `pdftotext`'s output operand
writes to standard output. Diagnostics go to standard error, never to standard
output. The only text encoding exercised is `-enc UTF-8`, which is also
`pdftotext`'s default; no other `ENC` value is used.

**Exit status.** For `pdfinfo`, `pdftotext` and `pdffonts` the poppler status
values are exercised and compared exactly: **0** on success, **1** when the PDF
could not be opened, **2** when the output file could not be opened, **3** on a
PDF permissions error, and **99** for any other error. In particular, a file whose
damage `pdftotext` cannot work around exits **99**, not 1 -- the recovery cases in
this milestone turn on that value, and reporting some other non-zero status fails
them. For `qpdf` the manual's three values are
all exercised: **0** when nothing was found wrong, **3** when the
operation succeeded but warnings were issued, **2** when errors were found. Three
situations in this milestone turn on the status rather than on any text: a
cross-reference chain qpdf must reconstruct exits **3**; a cross-reference stream
whose declared `/W` widths disagree with the length of its data exits **2**; and
`--filtered-stream-data` over a stream this milestone does not decode exits **2**
having written nothing at all to standard output. A utility that reports any of
these with exit 0, or that writes bytes in the third case, fails those cases.

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
| `optional` | one command over one file carrying optional content: a page of `pdftotext`, one `qpdf --show-object=N` of a dictionary that configures visibility, one `qpdf --show-pages` listing, or one `qpdf --show-linearization` dump |
| `objstm` | one command over one file that stores objects in an object stream: the `pdffonts` table, the compared field subset of `pdfinfo`, one `qpdf --show-object=N`, or one `qpdf --show-xref` listing |
| `crossref` | one `qpdf --show-xref` listing, one `qpdf --show-object=trailer`, one `qpdf --show-pages` listing, or one `qpdf --show-linearization` dump, over a file whose cross-reference information is a stream |
| `recovery` | one file whose cross-reference or object-stream data is damaged, or whose cross-reference information must be reconstructed: the exit status and the warnings that go with it, one `qpdf --show-pages` listing, or one `qpdf --show-linearization` dump |
| `jpx` | one `qpdf --show-object=N --raw-stream-data` or `--filtered-stream-data` over a stream whose filter chain names `JPXDecode` |

Every case belongs to exactly one group. The group is a property of the corpus file,
not of the command, so the same `qpdf --show-xref` listing is counted under `objstm`
for a file whose page tree is compressed and under `crossref` for a file exercising
`/W` and `/Index`; it is never counted twice.

The corpus also contains files within the previous milestones' scope, compared by those
milestones' rules: what worked there must keep working.

### 4.1 Exactly how outputs are compared

Unless a rule below says otherwise, the compared text is the utility's **standard
output only**, and the exit status must match as well.

**Expectations are generated, never written by hand.** Every expectation in this
milestone is recorded by running the reference tools — poppler-utils 22.12.0 and
**qpdf 11.3.0** — over the corpus file. This matters most for `--show-object`, whose
output is not a transcription of the file's bytes: it **re-sorts keys into ASCII
order**, it **deletes a key whose value is null**, and it **deletes a key whose
indirect reference does not resolve**, all silently and at exit 0. A hand-written
expectation will disagree with the reference on files that are perfectly correct, so
none is used.

* **text** (`optional`) — compared as a *character sequence*, not as a layout. Both
  sides are normalized: Unicode NFC, soft hyphen `U+00AD` removed, control characters
  replaced by a space, every run of whitespace collapsed to one space, leading and
  trailing whitespace stripped. Where the line breaks and how many spaces fall between
  words therefore do not matter; which characters the page contains, and in what order,
  does. This is the whole of the optional-content signal: the corpus pairs files whose
  content streams are byte-identical and whose visibility configuration differs, so the
  presence or absence of a run of text *is* the answer, at exit 0 with nothing on
  standard error. What is compared is the visibility outcome, never the route to it —
  `/OFF`, `/BaseState /OFF`, a membership dictionary and an XObject `/OC` are four
  ways to reach one observable result and are not told apart.
* **info** (`objstm`) — only the fields below are compared, as `Key: value` lines in
  this order, each value with internal whitespace runs collapsed. A field the report
  does not carry for a given file is simply absent from both sides:

  ```
  Title, Author, Creator, Producer, CreationDate, ModDate, Custom Metadata,
  Metadata Stream, Tagged, UserProperties, Suspects, Form, JavaScript, Pages,
  Encrypted, Page size, Page rot, File size, Optimized, PDF version
  ```

  `Subject` and `Keywords` are not compared. Lines outside this list are ignored, so
  emitting them is harmless. `Pages` is where this milestone's work shows, for a file
  whose page tree is reachable only through an object stream. `PDF version` is
  compared like every other field in the list, but it is **not** evidence about which
  structures a file uses: a file declaring 1.3 in its header may use every structure
  this milestone adds, and two of the corpus files do. No corpus file carries a
  `/Version` entry in its catalogue, so for every corpus file the field is the
  header's value.
* **fonts** (`objstm`) — the table is compared row by row, ignoring the header rule
  line, with runs of spaces collapsed to one. Row order is the order the real tool
  emits. One construct in the corpus turns on this comparison alone: an object defined
  **twice**, once plainly and once inside an object stream, with a different font in
  each. The cross-reference entry decides which definition is in force, and the base
  font name in this table is what reports the decision — at exit 0, with no warning.
* **object** (`optional`, `objstm`, `crossref`) — `qpdf --show-object`. Compared with
  whitespace runs collapsed to one space and leading and trailing whitespace stripped,
  so the line wrap and the two spaces in the stream header `Object is stream.
  Dictionary:` do not matter. Beyond the three deletions and re-ordering named above:
  1. keys are re-sorted into ASCII order, so uppercase keys precede lowercase ones;
     the authoring order in the file is not recoverable;
  2. reals are passed through **verbatim**, not renormalized: `.25` stays `.25` and
     `1.0` stays `1.0`, so the spelling of a number in the file is part of the answer;
  3. `#xx` escapes in names and `\)` escapes in strings survive; indirect references
     print as `n g R`; empty `<< >>` and `[ ]` are preserved;
  4. an object is printed **identically whether it is stored plainly or inside an
     object stream**. This output carries no evidence of compression; `--show-xref`
     below is the only place that distinction appears.
* **xref** (`crossref`, `objstm`) — `qpdf --show-xref`. Lines compared in order with
  whitespace runs collapsed. Two line shapes occur, and the second is new at this
  milestone:

  ```
  n/g: uncompressed; offset = D
  n/g: compressed; stream = S, index = I
  ```

  `D` is a byte offset into the corpus file and `S`/`I` are the object stream's number
  and the position within it, so all three are fixed by the file rather than chosen by
  the implementation. The object that matters is the one whose entry is the newest
  revision's, and for a compressed object that entry is the type-2 entry. Only files
  with an intact cross-reference chain produce cases in this group: where a file must
  be reconstructed, the offsets are an artefact of the reference tool's own recovery
  and are not compared.
* **trailer** (`crossref`) — `qpdf --show-object=trailer`. For a file whose
  cross-reference information is a stream, this prints that stream's dictionary, which
  is where `/W`, `/Index`, `/Prev`, `/Size`, `/Root`, `/Type /XRef` and `/ID` are
  visible. It is compared by the **object** rule above, ASCII key re-sorting included.
  This is the only command in the set that reports the field widths and the subsection
  layout at all.
* **stream data** (`jpx`) — `qpdf --show-object=N --raw-stream-data`: the SHA-256 of
  the bytes written to standard output must match, at exit 0. The bytes are the
  stream's contents before any filter is applied, so for a `JPXDecode` stream they are
  the stored codestream exactly, byte for byte, whatever it contains. The companion
  case is the refusal: `--filtered-stream-data` over the same stream must exit **2**
  with **zero bytes** on standard output, its warning compared by the **warnings**
  rule below. A chain of `[ /FlateDecode /JPXDecode ]` refuses the same way and
  produces no partial output — applying the filters you do implement and stopping is a
  wrong answer, not a partial one.
* **check** (`recovery`) — `qpdf --check`. Wrapped lines are joined and whitespace runs
  collapsed before comparison, and only these lines are compared:

  ```
  checking FILE
  PDF Version: V
  File is linearized            (or: File is not linearized)
  No syntax or stream encoding errors found; the file may still contain errors
  that qpdf cannot detect
  ```

  `FILE` is the operand exactly as it was given on the command line. Lines outside this
  list are ignored.
* **pages** (`optional`, `crossref`, `recovery`) — `qpdf --show-pages`. Compared as a
  sequence of lines, each with runs of whitespace collapsed and ends stripped, empty
  lines dropped, **order significant**. For each page in page-tree order it is the
  sequence `page N: n g R` (the page's number counting from 1, then its page object's
  reference), `content:`, then one `n g R` line per content stream of that page. The page
  and content object references are the answer; indentation is not.
* **linearization dump** (`optional`, `crossref`, `recovery`) — `qpdf --show-linearization`.
  Compared as `key: value` lines with whitespace collapsed and ends stripped. **The
  per-page entries of the Page Offsets Hint Table are NOT compared** and are dropped from
  both sides: their `length`, `content_length`, `nshared_objects` and `identifier` fields
  cannot be derived from the file by any field model in ISO 32000-1 Table F.3. Everything
  else in the dump IS compared, including all eight linearization-data fields, all
  thirteen Page Offsets Hint Table *parameters*, and the entire Shared Objects Hint Table.
  A file that is not linearized produces the single line `FILE is not linearized` on
  standard output, `FILE` being the operand exactly as it was given on the command line,
  and that line is the answer.
* **warnings** (`recovery`, `jpx`) — the exit status must match exactly, and so must
  the set of warnings. A warning's identity is derived like this: take the lines
  beginning `WARNING:`, drop that prefix, drop a following `FILE: ` or
  `FILE (offset D): ` prefix, replace every run of decimal digits with `#`, collapse
  whitespace, and compare the results as a **set** — unordered, duplicates removed.
  The trailing line `qpdf: operation succeeded with warnings` is not part of the set;
  the exit status already carries it. This is the one comparison made over standard
  output and standard error **merged**, so that it does not depend on which of the two
  a warning arrives on. Where a damaged file is also expected to produce an answer,
  that answer is compared by the rule for it above.

  **The identities this milestone's corpus actually turns on.** An implementation cannot
  guess a diagnostic's wording, and the set comparison above is over exact strings, so
  the vocabulary is stated here rather than left to be discovered. `FILE` stands for the
  operand as the tool was given it, and `#` for a run of decimal digits, both exactly as
  the normalization above produces them. Emitting one of these for a file that does not
  warrant it fails the case as surely as omitting it: the comparison is a set, not a
  checklist.

  ```
  Attempting to reconstruct cross-reference table
  FILE (object # #, offset #): object stream # has incorrect keys
  FILE (object # #, offset #): supposed object stream # has wrong type
  FILE (xref stream, offset #): Cross-reference stream data has the wrong size; expected = #; actual = #
  FILE, stream object # #: unable to filter stream data
  file is damaged
  object # #: operation for dictionary attempted on object of type null: returning false for a key containment request
  xref not found
  ```


**A documented limit, and the reason `--show-xref` matters at this milestone.** Three
files with identical content and an identical header, differing only in whether their
cross-reference information is a classic table, a cross-reference stream, or a
cross-reference stream with an object compressed into an object stream, produce
**identical** output from `pdfinfo`, `pdftotext`, `pdffonts` and `qpdf --check`. No
case therefore asks any of those four which structure a file uses; they show only
whether the document reads *at all*, which for a file whose page tree is compressed is
already a real question. `qpdf --show-xref` is the only command in the set that
reports compression, and it is where the structural obligations of this milestone are
actually visible.

**A documented necessity.** Two files in the corpus are identical but for where the
page-tree root lives — plainly in one, inside an object stream in the other — and both
have their `startxref` corrupted so the cross-reference information must be
reconstructed. Reconstruction by scanning finds the plain root and the file reads at
exit 0; it cannot find the compressed one, and that file fails. Both answers are
expected as they stand. This is the one case where recovery is *expected* to fail, and
it is what separates an implementation that genuinely reads object streams from one
that scans the file and hopes.

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
* `/app/SPEC/pdf15.md` — this milestone's requirements in the standard's terms, with
  `/app/SPEC/pdf14.md` and `/app/SPEC/pdf13.md` for the previous milestones', all of
  which still apply.
