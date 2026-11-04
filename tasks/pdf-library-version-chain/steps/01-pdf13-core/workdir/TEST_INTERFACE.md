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
qpdf --show-object=N[,G] [--filtered-stream-data | --raw-stream-data] FILE
```

Page numbers on the command line are 1-based. `-` as `pdftotext`'s output operand
writes to standard output. Diagnostics go to standard error, never to standard
output. Exit status is compared exactly, not merely as zero versus non-zero: **0** on success,
**1** when the PDF could not be opened, and **2** when the output file could not be
opened. Those are poppler's own values, and the matching one is required.

The only text encoding exercised is `-enc UTF-8`, which is also `pdftotext`'s default;
no other `ENC` value is used.

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
| `text` | one page of `pdftotext -f N -l N -enc UTF-8 FILE -` |
| `filters` | one object of `qpdf --show-object=N --filtered-stream-data FILE` (and `--raw-stream-data`) |
| `info` | the compared field subset of `pdfinfo FILE` for one file |
| `fonts` | the `pdffonts FILE` table for one file |
| `robust` | a damaged file that a conforming reader still recovers, or a file so broken that the only correct behaviour is to fail with a non-zero exit |

### 4.1 Exactly how outputs are compared

* **text** — compared as a *character sequence*, not as a layout. Both sides are
  normalized: Unicode NFC, soft hyphen `U+00AD` removed, control characters replaced
  by a space, every run of whitespace collapsed to one space, leading and trailing
  whitespace stripped. Where the line breaks and how many spaces fall between words
  therefore do not matter; which characters the page contains, and in what order,
  does. A wrong glyph, a dropped ligature or a broken `/ToUnicode` still fails.
* **info** — only the fields below are compared, as `Key: value` lines in this
  order, each value with internal whitespace runs collapsed. A field the report does
  not carry for a given file is simply absent from both sides:

  ```
  Title, Author, Creator, Producer, CreationDate, ModDate, Custom Metadata,
  Metadata Stream, Tagged, UserProperties, Suspects, Form, JavaScript, Pages,
  Encrypted, Page size, Page rot, File size, Optimized, PDF version
  ```

  `Subject` and `Keywords` are not compared. Lines outside this list are ignored, so
  emitting them is harmless.
* **fonts** — the table is compared row by row, ignoring the header rule line, with
  runs of spaces collapsed to one. Row order is the order the real tool emits.
* **filters** — the SHA-256 of the bytes written to standard output must match.
* **robust** — the exit status must match, and where an answer is expected the
  comparison is the one for that output above.

## 5. Checking progress

A sample corpus with its expected answers, built from the same reference tools, ships
in the image:

```
make && bash /opt/pdf-tests-public/run-public-tests.sh
bash /opt/pdf-tests-public/run-public-tests.sh text info    # named groups only
```

It covers only part of the milestone: passing it does not mean the milestone is met, but
a utility that fails it has not met the milestone either.

## 6. Normative documents in this image

* `/opt/pdfspec/ISO-32000-1-2008.txt` — the published ISO 32000-1:2008 text
  (PDF 1.7), the normative definition of the file format.
* `/opt/pdfspec/man-pdfinfo.txt`, `man-pdftotext.txt`, `man-pdffonts.txt` — the
  poppler-utils man pages that define those three interfaces.
* `/opt/pdfspec/qpdf-running-qpdf.txt` — the qpdf manual's command-line chapter,
  which defines `--show-object`, `--filtered-stream-data` and `--raw-stream-data`.
* `/app/SPEC/` — this milestone's requirements, in the standard's terms.
