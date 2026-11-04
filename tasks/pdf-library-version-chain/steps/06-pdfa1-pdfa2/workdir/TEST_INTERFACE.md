# TEST_INTERFACE.md — how this repository is built and driven, and how its output is compared

This file is authoritative for the mechanics of this workspace: the build command, the
programs and option spellings in use, the environment hooks, and exactly how
each output is compared with the reference.

## 1. Build

`make`, run in `/app`, builds every utility into `/app/build/` under its real program
name. That exact command — plain `make`, no added flags, with `/app` as the working
directory — is how the utilities are built:

```
make            # -> /app/build/pdfinfo, /app/build/pdftotext, /app/build/pdffonts,
                #    /app/build/qpdf, /app/build/verapdf, ...
make clean
```

The utilities are invoked with `/app` as their working directory, and are given a
`PATH` containing only `/app/build`, so they must not depend on any other program
being reachable by name. Each utility is self-contained: it starts no other program, opens no network
connection, and loads no code beyond its own binary and the C runtime.

They are also run with `LC_ALL=C.UTF-8`. Do not depend on the ambient locale for case
folding, collation, or number formatting; this one is pinned, and output is compared as bytes.

The workspace must not contain the *sources* of third-party PDF engines — MuPDF, pdfium,
poppler, qpdf, HarfBuzz, Ghostscript and veraPDF, each recognized by its own public symbol
names. This rule is about vendoring someone else's engine, not a restriction on your naming:
it matches whole identifiers, ignores comments, and covers no name a library written from
scratch would arrive at on its own. Writing your own `pdf_load_page()` is fine; shipping
MuPDF's `fz_open_document()` is not, and neither is shipping veraPDF's validation profiles
as data.

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
qpdf, no mupdf, no ghostscript, no pdfium, no veraPDF, no Java runtime, no PDF module for
python. A utility that calls out to a system PDF tool therefore finds none;
`/app/build/verapdf` is your own C program that happens to wear that interface, and nothing
of that name exists in the container for them to reach.

Leave `/app` source-only. The workspace is archived and restored when it is handed
on, so delete build products outside `build/` before you finish, and
compile scratch programs under `/tmp`.

## 2. The utilities and their options

The programs are **not** this task's invention. Each is an existing published
command-line interface adopted as-is, with the same program name, the same option
letters, the same operand order and the same output layout:

* `pdfinfo`, `pdftotext`, `pdffonts` — **poppler-utils**, as defined by the
  `pdfinfo(1)`, `pdftotext(1)` and `pdffonts(1)` man pages.
* `qpdf` — the **qpdf** command-line tool, as defined by the qpdf manual
  ("Running qpdf").
* `verapdf` — the **veraPDF** command-line validator, as defined by its usage text.

Only the options listed below are exercised. Behaviour for all of them is the
behaviour those documents describe. The earlier milestones' options are carried
forward unchanged. **This milestone's own corpus holds PDF/A conformance fixtures and
its malformed group**; this milestone adds one program:

```
verapdf --flavour FLAVOUR --format text FILE...
verapdf -f FLAVOUR --format text FILE...
```

`FLAVOUR` is one of `1a`, `1b`, `2a`, `2b`, `2u`; `-f` and `--flavour` are the same
option and both spellings are exercised. `--format text` is the only output format
exercised — `xml`, `json`, `html` and `raw` are named by the real interface and are
not used, so they may be emitted or omitted. Automatic
flavour detection (`-f 0`, or no `-f` at all) is **not** exercised: every
invocation names a flavour. `verapdf` takes any number of `FILE` operands and emits
one line per file, in operand order.


**Exit status.** For the earlier utilities it is what their milestones fixed. For
`verapdf` the measured values of the real tool are all exercised: **0** when every
file processed in the invocation is compliant, **1** when at least one is not
compliant and none failed to parse, **7** when at least one operand could not be
parsed, **4** when the invocation ends up with no file to process at all. Note that 0 and 1 are
*verdicts* while 4 and 7 are *operational* errors: "the file is not compliant" and
"the file could not be read" are different answers and are treated as different
answers. An operand naming a file that does not exist is **dropped**: it contributes
no line, it does not make the invocation fail, and the status reflects only the files
actually processed.

## 3. Environment hooks

* **`SOURCE_DATE_EPOCH`** (the reproducible-builds.org specification) — when set, any
  timestamp a utility writes into a file it produces uses that instant instead of the
  wall clock. It is always set when the utilities run. The rest of their environment is
  pinned as well -- `PATH` to the build directory, `LC_ALL=C.UTF-8`, and `HOME` and
  `TMPDIR` to `/tmp` -- so that a utility writing a temporary file behaves exactly as it
  does under your own `make && run-public-tests.sh`. None of those four selects behaviour; the
  hook above is the only thing in this workspace that does. No invocation at this
  milestone writes a file, so the hook has nothing to stamp here. It is documented because
  it is set in the environment every invocation runs in, not because this milestone
  produces anything for it to affect.

Input and output paths are the operands the interfaces above already define. There is
no mode flag, no profile switch, no version banner and no marker string in this
workspace; behaviour is selected by the options above and by the content of the file.

## 4. What the output must match

The utilities are run over a corpus, and their output must match answers recorded from a
real independent implementation. The cases fall into these groups:

| group | what one case is |
| --- | --- |
| `pdfa1b` | one file's verdict line from `verapdf -f 1b`, over a corpus file exercising an ISO 19005-1 level-B clause |
| `pdfa2b` | one file's verdict line from `verapdf -f 2b`, over a corpus file exercising an ISO 19005-2 level-B clause |
| `levels` | one file's verdict line from `verapdf -f 1a`, `-f 2a` or `-f 2u`, over a corpus file exercising a character-map or conformance-identification clause |
| `malformed` | one operand the checker cannot parse, or that is absent: the line it does or does not produce, and the invocation's exit status |

Every case belongs to exactly one group. The group is a property of the corpus file
and the flavour it is validated at, not of the command, so the same file validated at
`1b` and at `2b` yields one case in each group and is not counted twice.


### 4.2 The declared rule set

This milestone's validator covers a DECLARED SUBSET of ISO 19005-1 / ISO 19005-2,
not the whole of it. Every `FAIL` file in the corpus fails because of one
or more of the clauses below, and no file fails for any other reason, so a
checker that implements exactly these clauses gets every verdict right:

* clause `6.1.2` test `1`
* clause `6.2.3.3` test `3`
* clause `6.2.4` test `3`
* clause `6.2.4.2` test `2`
* clause `6.2.6` test `1`
* clause `6.3.1` test `1`
* clause `6.3.2` test `1`
* clause `6.3.3` test `1`
* clause `6.4` test `3`
* clause `6.4` test `4`
* clause `6.4.2` test `1`
* clause `6.5.1` test `1`
* clause `6.6.2.1` test `1`
* clause `6.6.4` test `3`
* clause `6.7.11` test `2`
* clause `6.7.11` test `3`
* clause `6.7.3` test `1`
* clause `6.7.3` test `4`
* clause `6.7.3` test `5`
* clause `6.7.3` test `6`
* clause `6.7.3` test `7`
* clause `6.7.3` test `8`
* clause `6.9` test `2`

That is **23 distinct rules**. One more rule
is reached only through the `malformed` group: the one non-conforming file its mixed
batches carry fails `1b` on clause `6.1.12` test `4` alone, a name object longer than 127
bytes (`/app/SPEC/pdfa1-2.md` §2.4). Clauses outside these 24 are not exercised, so they
need not be implemented. The `PASS` files are NOT restricted in the same
way -- a conforming file is conforming under every clause, and answering `PASS` for it is
correct whatever your checker implements.

### 4.1 Exactly how outputs are compared

**The expectations are the verdicts of veraPDF 1.30.2**, the pinned release of the
reference validator, over a corpus pinned by commit. They are generated by running it,
never written by hand. Nothing in this section compares your output against a
transcription of a standard: it compares it against what that program actually said.

**Validation runs as one batched invocation.** All the files for a
flavour are passed to a single `verapdf` invocation as many `FILE` operands, not one
invocation per file, and that is how your `verapdf` is run. A program that is
correct but pays a fixed start-up cost per file will not finish: the reference tool
takes roughly 26-39 ms per file batched against roughly 2,800 ms per file when
re-started for each one, and the budget assumes the former. Your program must accept
the whole operand list at once.

**Only standard output is compared.** Standard error is never compared, for either
program, in any group. The reference tool writes wall-clock-stamped logging lines
there for some inputs, which are not reproducible even by itself; whatever your
programs write there is ignored, and writing nothing is equally fine.

* **verdict lines** (`pdfa1b`, `pdfa2b`, `levels`) — `verapdf --format text` emits
  exactly one line per processed operand. Three line shapes occur, and all three are
  compared:

  ```
  PASS PATH FLAVOUR
  FAIL PATH FLAVOUR
  PATH does not appear to be a valid PDF file and could not be parsed.
  ```

  `PATH` is the operand as the tool reports it, and this rule was measured rather than
  assumed. An **absolute** operand is echoed back byte for byte. A **relative** one has
  the working directory prepended: `p.pdf` run from `/s` comes back `/s/p.pdf`. Nothing
  is normalized — `./p.pdf` comes back `/s/./p.pdf`, and `../s/p.pdf` comes back
  `/s/../s/p.pdf`, both keeping the `.` and `..` exactly where they were written.
  **Absolute paths are therefore passed**, which are echoed unchanged, so the
  same string appears on both sides and no working directory enters the comparison:
  echo an absolute operand exactly as it was given and this field is right.
  `FLAVOUR` is the flavour that was requested, echoed back. Lines are
  compared as a **set**, sorted before comparison, with runs of whitespace collapsed
  to one space: the emission order of the real tool was measured stable across runs
  and across batch orderings, but nothing depends on it. The line count must match,
  which is what catches a dropped operand.
* **the verdict is the whole of the answer.** For a corpus file the answer is
  one of three tokens and a path; a line is right or wrong as a whole, and a plausible
  diagnostic does not make a wrong verdict right. A file the reference tool calls `PASS`
  that you call `FAIL` is wrong in exactly the way a file it calls `FAIL` that you call
  `PASS` is wrong. Reporting the clause that failed is not required: `--format text` does not carry
  it, and no comparison reads it.
* **exit status** (`pdfa1b`, `pdfa2b`, `levels`, `malformed`) — the status of each
  batched invocation is compared, under §2's rule. Because a batch mixes compliant
  and non-compliant files, the status is a property of the batch and **not** a
  per-file verdict; it is what distinguishes a batch in which something failed to
  parse (**7**) from one in which everything parsed and something was merely
  non-compliant (**1**). Do not try to signal a per-file verdict through it.
* **malformed and absent operands** (`malformed`) — a file that is not a PDF, or is
  truncated past use, produces the third line shape above **on standard output** and
  makes the invocation exit **7**; the file is named but no verdict is claimed for
  it. An operand that names a file that does not exist produces **no line at all**,
  does not change the exit status, and leaves the other operands' lines exactly as
  they would have been. Both are compared by the rules above — which for the absent
  operand means the expected property is an absence, and a program that invents a
  `FAIL` line for a file it never opened fails the case.
## 5. Checking progress

A sample corpus with its expected answers, built from the same reference checker, ships
in the image:

```
make && bash /opt/pdf-tests-public/run-public-tests.sh
bash /opt/pdf-tests-public/run-public-tests.sh text robust   # named groups only
```

It is a **sample of the EARLIER milestones' behaviours**, and it checks only
`exit`, `text`, `info`, `fonts`, `stream` and `raw` cases. **None of this milestone's own
cases is of those kinds**, so the sample is a regression check on what you built before,
not evidence about this milestone in either direction: passing it does not mean you are
close here, and failing it does not mean you are not. Section 4 above is what this
milestone's output must match.

## 6. Normative documents in this image

* `/app/SPEC/pdfa1-2.md` — this milestone's requirements in the standards' terms,
  clause by clause against ISO 19005-1:2005 and ISO 19005-2:2011.
* `/opt/pdfspec/ISO-32000-1-2008.txt` — the published ISO 32000-1:2008 text, the
  normative definition of the underlying file format.
* `/app/SPEC/verapdf-cli.txt` — the veraPDF command-line usage: `--flavour`,
  `--format`, the operand list and the exit status.
* `/opt/pdfspec/man-pdfinfo.txt`, `man-pdftotext.txt`, `man-pdffonts.txt` and
  `/opt/pdfspec/qpdf-running-qpdf.txt` — the four earlier utilities' interfaces.
* `/app/SPEC/pdf13.md`, `pdf14.md`, `pdf15.md`, `pdf16-17.md`, `pdf20.md` — the
  previous milestones' requirements, all of which still apply.
