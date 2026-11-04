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
                #    /app/build/qpdf, /app/build/pdfimages, /app/build/pdfsig,
                #    /app/build/verapdf, /app/build/pdftoppm, ...
make clean
```

The utilities are invoked with `/app` as their working directory, and are given a
`PATH` containing only `/app/build`, so they must not depend on any other program
being reachable by name. Each utility is self-contained: it starts no other program, opens no network
connection, and loads no code beyond its own binary and the C runtime.

They are also run with `LC_ALL=C.UTF-8`. Do not depend on the ambient locale for case
folding, collation, or number formatting; this one is pinned, and output is compared as bytes.

The workspace must not contain the *sources* of third-party PDF and graphics engines —
MuPDF, pdfium, poppler, cairo, qpdf, HarfBuzz, Ghostscript and veraPDF, each recognized by
its own public symbol names. This rule is about vendoring someone else's engine, not a
restriction on your naming: it matches whole identifiers, ignores comments, and covers no
name a library written from scratch would arrive at on its own. Writing your own
`pdf_render_page()` is fine; shipping poppler's `SplashOutputDev` or cairo's
`cairo_fill_preserve()` is not.

The **built binaries** are covered as well, separately from the sources: in
`/app/build/`'s utilities, `strings` must find none of these vendor markers
(`MuPDF`, `Artifex`, `Ghostscript`, `poppler`, `pdfium`, `libqpdf`, `qpdf version`, `xpdf`,
`HarfBuzz`, `FreeType`, `cairo`), and no symbol `readelf` lists may carry one of these
third-party prefixes (`fz_`, `FPDF`, `opj_`, `jbig2_`, `hb_`). Unlike the source rule, the
marker rule applies **anywhere in the binary**, including data sections — because a
`Makefile` recipe could fetch a prebuilt engine and install it as `/app/build/pdftoppm`, which
would satisfy every other rule. None of those markers is a name a library written from
scratch produces on its own.


The implementation language is **C** (C11). The image provides `gcc`, `make`,
`binutils`, `gdb` and `valgrind`, plus `zlib`.

The utilities must be compiled ELF binaries produced by that `make`, and
each one's dynamic dependencies must lie within the C runtime and zlib:

```
libc.so  libm.so  libz.so  libgcc_s.so  libpthread.so  libdl.so  ld-linux
```

**`libpng` is not on that list, and neither is `libfreetype`.** The image file this
milestone writes is produced by your own code against zlib, and glyphs are rasterized by
your own code from the font programs the earlier milestones already parse. Neither is an
oversight; both are the milestone.

No utility may be a script, link anything outside that list, or be byte-identical to
another binary shipped in the workspace. Static linking is fine. Each utility must be a
regular file: a symbolic link in a utility's place is removed before the utilities are run,
and that utility counts as not built. The workspace must build from source with `make` in a
container that has a C toolchain and **no PDF implementation and no raster engine of any
kind** — no poppler, no cairo, no qpdf, no mupdf, no ghostscript, no pdfium, no veraPDF, no
Java runtime, no PDF or imaging module for python. A utility that calls out to a system tool
therefore finds none; `/app/build/pdftoppm`, `/app/build/verapdf` and the other utilities
are your own C programs that happen to wear those interfaces, and nothing of that name
exists in the container for them to reach.

Leave `/app` source-only. The workspace is archived and restored when it is handed
on, so delete build products outside `build/` before you finish, and
compile scratch programs under `/tmp`. Rendering a corpus produces a great deal of
output; write it under `/tmp`, never into `/app`.

## 2. The utilities and their options

The programs are **not** this task's invention. Each is an existing published
command-line interface adopted as-is, with the same program name, the same option
letters, the same operand order and the same output layout:

* `pdfinfo`, `pdftotext`, `pdffonts`, `pdfimages`, `pdfsig` — **poppler-utils**, as
  defined by their man pages.
* `pdftoppm` — **poppler-utils**' page rasterizer. Its options are fixed by §2.1 below
  and by `/app/SPEC/raster-linearize.md` §1, in full: unlike the other five, **no man
  page for it ships in this image**, so those two sections are its whole definition here.
* `qpdf` — the **qpdf** command-line tool, as defined by the qpdf manual
  ("Running qpdf").
* `verapdf` — the **veraPDF** command-line validator.

Only the options listed below are exercised. The earlier milestones' options are carried
forward unchanged. **This milestone's own corpus holds five groups** --
`raster`, `shading`, `composite`, `linearize` and `damaged` -- and the earlier
options are exercised by those files. This milestone adds one program and two `qpdf`
options:

```
pdftoppm -png -r 150 -aa yes -aaVector yes -freetype yes -thinlinemode none \
         -f N -l M INFILE OUTPREFIX
pdftoppm -png -r 150 -aa yes -aaVector yes -freetype yes -thinlinemode none \
         -f N -l N -singlefile INFILE OUTPREFIX
qpdf --linearize INFILE OUTFILE
qpdf --check-linearization FILE
```

`-f` and `-l` are the first and last page, 1-based and inclusive, as the other poppler
programs already use them. Exactly one `INFILE` is given per invocation.

### 2.1 The frozen render command

**Those six render flags appear on every render invocation and never vary.** Three of
them name the program's own default and are passed explicitly anyway, so that the
command line is the whole specification of the rendering and nothing is left to a default
that a future build might change.

They are frozen because each was measured to change the output bytes, or measured not to:

| flag | frozen value | measured effect of varying it |
| --- | --- | --- |
| `-png` | always | the default PPM output and `-tiff` both differ. The format is pinned |
| `-r` | `150` | `-r 149` and `-r 300` both differ. `-r 150.0` and `-rx 150 -ry 150` are identical to `-r 150`, so the numeric spelling is free |
| `-aa` | `yes` (the default) | `-aa no` differs on every content class measured |
| `-aaVector` | `yes` (the default) | `-aaVector no` differs on vector content; on a text-only page it made no difference, so a test that used only text would have called this flag inert |
| `-freetype` | `yes` (the default) | `-freetype no` differs on every content class measured |
| `-thinlinemode` | `none` (the default) | `none`, `solid` and `shape` give **three distinct outputs** on strokes narrower than one device pixel — and are indistinguishable on strokes wider than one. See the warning below |

Flags that are **never passed**, each left at its default: `-gray`, `-mono`, `-jpeg`,
`-jpegcmyk`, `-tiff`, `-overprint`, `-progress`, `-q`, the crop and scale options
(`-x`, `-y`, `-W`, `-H`, `-sz`, `-scale-to`, `-scale-to-x`, `-scale-to-y`), the page
selectors `-o` and `-e`, `-sep`, `-forcenum`, the password options, and the four ICC
options (`-displayprofile`, `-defaultgrayprofile`, `-defaultrgbprofile`,
`-defaultcmykprofile`). `-gray` and `-overprint` were both measured to change the bytes,
which is exactly why they are not passed: an implementation that applies one anyway
fails every case in the group.

**Two flags were exercised and showed no effect, and that is not the same as inertness.**
`-cropbox` and `-hide-annotations` produced byte-identical output — but the pages they
were exercised against had no `/CropBox` different from the `/MediaBox` and carried no
annotations, so neither flag had anything to act on. They are pinned (never passed)
rather than declared harmless, and no case depends on their behaviour.

**The `-thinlinemode` warning.** Against 0.5 pt strokes the three values give identical
output and the flag looks inert: at 150 dpi a 0.5 pt stroke is 1.04 device pixels — just
above the threshold where the setting does anything. Against strokes of 0, 0.05, 0.1 and
0.25 pt, the same three values give three different renderings. Any page with
hairlines — a rule, a table border, a CAD drawing — is sensitive to it. The general rule
this is an instance of: **a flag that appears to do nothing may simply not have been
exercised where it acts**, and one measurement over one input is not evidence of
inertness.

**The engine is part of the specification.** A second rasterizer with the same interface
shape exists in the world (`pdftocairo`), and it was measured to disagree with this one
byte-for-byte on every content class. `pdftoppm` is the interface this milestone adopts;
the other is not in this workspace, not used, and not a permissible substitute.

### 2.2 What the render writes

`pdftoppm` writes one file per page, named `OUTPREFIX-NNN.png`, where `NNN` is the page
number zero-padded. With `-singlefile` it writes exactly `OUTPREFIX.png` for the single
page requested and adds no digits at all; a single-page invocation with `-singlefile` and
the corresponding page of a whole-document invocation were measured to produce **identical
bytes**, so the shape of the invocation is not part of the answer.

The zero-padding width is **not compared**: the numeric suffix of each file the
invocation produced is read as the page number. What *is* compared is the
number of files (§4.1) and their contents.

### 2.3 Exit status

For the earlier utilities it is what their milestones fixed. `qpdf`'s three documented
values continue to be exercised: **0** when nothing was found wrong, **3**
when the operation succeeded but warnings were issued, **2** when errors were found.

**`pdftoppm`'s exit status is not compared in any case at this milestone.** What is
compared is the set of files it produced and what is in them. Two reasons, and the second is
the more important: that program's status was not recorded with the reference outputs,
and an unrecorded value must not become an expectation; and an exit status is in general the weakest
available evidence that a renderer did anything — see the trap in §4.1, which was measured
on the other half of this milestone and generalises to this one.

## 3. Environment hooks

* **`SOURCE_DATE_EPOCH`** (the reproducible-builds.org specification) — when set, any
  timestamp a utility writes into a file it produces uses that instant instead of the
  wall clock. It is always set when the utilities run. The rest of their environment is
  pinned as well -- `PATH` to the build directory, `LC_ALL=C.UTF-8`, and `HOME` and
  `TMPDIR` to `/tmp` -- so that a utility writing a temporary file behaves exactly as it
  does under your own `make && run-public-tests.sh`. None of those four selects behaviour; the
  hook above is the only thing in this workspace that does. It applies to the file `qpdf --linearize` writes, whose
  document information dictionary and `/ID` may carry dates; it does **not** make that
  file byte-reproducible, and nothing in §4 requires that it should. It has **no effect
  whatever on a rendered page**, because the image file carries no timestamp field for it
  to set — which was measured, not assumed: `SOURCE_DATE_EPOCH=0` was one of ten
  environment variants over which the rendered bytes were identical.

The other nine were `HOME=/work`, `HOME=/tmp`, `TZ=Asia/Tokyo`, `LC_ALL=C`,
`LC_ALL=C.UTF-8`, `XDG_CACHE_HOME=/tmp/xdgcache`, one CPU, eight CPUs, and the default.
None of them changed a byte of any rendered page. Do not read anything about how to draw
from the environment; there is nothing there to read.

**Standard error is never compared, in any group.** The reference renderer emits a
font-cache diagnostic on some invocations that has no effect on the pixels — the same
page rendered with that diagnostic and without it was byte-identical — and whatever your
programs write there is ignored. Writing nothing is equally fine, with no exception:
the `damaged` group here is compared as `raster` and `absent` cases, so no warning text is
compared anywhere in this milestone.

Input and output paths are the operands the interfaces above already define. There is no
mode flag, no profile switch, no version banner and no marker string in this workspace;
behaviour is selected by the options above and by the content of the file.

## 4. What the output must match

The utilities are run over a corpus, and their output must match answers recorded from
real independent implementations. The cases fall into these groups:

| group | what one case is |
| --- | --- |
| `raster` | one page of one corpus document, rendered by the §2.1 command: the raster it produced |
| `linearize` | one `qpdf --linearize` output, judged by an Annex F structural check; or one `qpdf --check-linearization` report over a file whose linearization data is correct, absent or damaged |
| `shading` | one page whose content paints a shading of one of the seven types |
| `composite` | one page whose content uses constant alpha, a blend mode, or a soft mask |
| `damaged` | one page of a document whose object, stream or cross-reference data is damaged: the raster it produced, or the absence of one |

Every case belongs to exactly one group. The group is a property of the corpus page, not
of the command: a page that paints a shading *through* a soft mask is assigned to one
group when the corpus is built and is never counted twice. Every page in this corpus was
built for this milestone and belongs to one of the five groups above.

Each page is its own case, and there is no tolerance inside a page: see §4.1.

### 4.1 Exactly how outputs are compared

**The expectations are renderings by poppler-utils 22.12.0 and reports by qpdf 11.3.0**,
run under a pinned image digest, over a corpus pinned by commit. They are generated by
running those programs, never written by hand. Nothing in this section compares your
output against a transcription of a standard: it compares it against what those programs
actually produced.

* **raster comparison** (`raster`, `shading`, `composite`, `damaged`) — for each page, the
  file your `pdftoppm` wrote is decoded, and the compared value is the **SHA-256 of the
  decoded raster**, serialized canonically as the ASCII header `RGB8 W H` and a newline
  followed by the samples in row-major order, three bytes per pixel, red first. It must
  equal the reference's. There is no tolerance, no threshold and no per-pixel allowance:
  the page is right or it is not.

  Two structural preconditions are checked first, and a case that fails either fails
  without being hashed: the file must be a well-formed PNG that is 8-bit RGB,
  non-interlaced and not palettized — which is what the reference writes — and its width
  and height must equal the reference's. Nothing else about the container is compared: the
  `pHYs` chunk, the number and size of the `IDAT` chunks, the scanline filters chosen and
  the compression level are all yours to pick.

  **Why the container is excluded, when the raster is not.** The reference PNG files are
  themselves byte-stable — that was measured, and §4.2 explains why they are — so hashing
  the file would have worked. It would also have measured something other than rendering:
  the exact deflate byte stream is a property of the compression library and of the
  encoder's buffering, not of the page, and this milestone bars you from linking the
  encoder the reference used. Requiring you to reproduce another library's compression
  choices would test the container and call it a rasterizer.

* **the count assertion** (`raster`, `shading`, `composite`) — **the number of files the
  invocation produced must equal the number of pages it was asked for.** This is not a
  formality. A page your renderer declines to draw leaves no file, and a file that is not
  there cannot differ from anything, so the count is checked first, and each missing page
  is a failed case in its own right.

* **linearized output** (`linearize`) — `qpdf --linearize IN OUT`. Checked in three parts,
  in order, and a case passes only if all three hold:
  1. `qpdf` exits **0** and `OUT` exists;
  2. `OUT` is judged by an **Annex F structural check**, never by your own `qpdf` and
     never by a PDF tool inside the image — §1's container has none, deliberately.
     The check reads what ISO 32000-1 Annex F defines: the linearization parameter
     dictionary and its `/L`, `/H`, `/O`, `/E`, `/N` and `/T`, the first-page and main
     cross-reference sections, the presence of the hint stream `/H` points at, and the
     order of the parts. **The hint tables' bit fields are not decoded**, and neither
     are the shared-object lengths: the check is conservative in exactly that
     direction, reporting a fault only where the file contradicts itself. Measured
     against qpdf 11.3.0 over a 191-file battery it agrees on 188, with **zero false
     rejects** and three false accepts (a corrupted hint bit stream, a `-dFastWebView`
     shared-object length mismatch, and one `/E`). The check's verdicts were fixed
     against **qpdf 11.3.0** when the corpus was built, over a battery that includes files
     that tool accepts, files it rejects, and near-misses such as a correct hint table
     under a wrong `/T` — so what decides is still that tool's judgement, reached
     without putting it in the room;
  3. `OUT` still contains the document: its page count matches the input's, and its
     extracted text matches the input's under the `text` normalization the earlier
     milestones define.

  **`OUT` is not compared byte for byte**, against a reference output or against itself.
  Object numbering, stream compression and the placement of objects within a part all
  admit many correct answers. There is no expected output file for any linearization case.

* **the exit-code trap, stated because it is the one that looks safe** — part 2 above
  matches **standard output**, not the exit status, and the difference is the whole test.
  The reference checker exits **0** for all three of: a correctly linearized file
  (`FILE: no linearization errors`), a file that is **not linearized at all**
  (`FILE is not linearized`), and a file whose `/L` is damaged badly enough that the
  dictionary stops being recognized (`FILE is not linearized` again). A check of
  `$? -eq 0` alone would therefore accept an implementation that never implemented
  linearization and simply copied the input. **That absent case is in the corpus**,
  precisely because it is the one nobody thinks to construct: it feels like it must
  obviously fail, and it does not. Exit **3** is the interesting failure — it means the
  file claims linearization and the claim does not hold — and it was measured for damage
  to `/O`, `/E`, `/T`, `/N` and to the hint stream, each with its own warning.

* **linearization reports** (`linearize`) — `qpdf --check-linearization FILE`, compared as
  the earlier milestones compare qpdf's reports: standard output lines with whitespace
  runs collapsed and the exit status exactly. The report's own lines are compared **in
  order**, not as an unordered set: `--check-linearization` emits at most the two verdict
  forms, so there is no set to reorder, and an empty stdout is itself a real answer
  because an inconsistent file writes its diagnosis to standard error instead. The
  fixtures are a correctly linearized file, a file that is not linearized, and one
  file for each of the five kinds of internal inconsistency named above.

* **how strict the check is, stated plainly** — the bar part 2 sets is *this checker's*
  notion of a correct hint table, not "any reader accepts it". Two independent real-world
  linearizers were pointed at the same source file and **both were rejected**: one
  produced shared-object lengths its own hint table contradicted, the other a bit stream
  that overran while being read. Neither is a rubber stamp and neither is a neutral
  standards test. The check is bimodal and cannot be bluffed, and you should know which
  way it is strict before you aim at it.

* **damaged input** (`damaged`) — a document whose stream length, cross-reference data or
  filter data is damaged is still rendered: the reference recovers and paints the page,
  and does so **deterministically** — the same damaged page rendered five times across
  separate containers produced one distinct result each time, for each of three kinds of
  damage. Those pages are compared exactly like any other. A document truncated past the
  point where its page tree can be found produces **no file at all**, and for that case the
  expected property is the absence: a program that writes a blank page fails it.

### 4.2 Why the comparison is exact, and why no similarity measure replaces it

Exactness here is not severity for its own sake. It rests on two measured facts and one
argument.

**First: nothing in the pipeline varies.** The rendered page carries no timestamp and no
producer string — the chunk inventory of the reference output is the header, a physical
dimensions chunk, the image data and the end marker, and nothing else. There is no `tIME`,
no `tEXt`, no `Software`. That is a structural reason for stability rather than a lucky
sample, and it held: one distinct result across three renders in one container, two more
containers, a different mount point and hostname, ten environment variants, one CPU
against eight, and a host under heavy load from other work; across three synthetic feature
classes and two pages of a 756-page production document; and across 300 pages rendered
once as a batch and again as 300 separate parallel invocations, **300 of 300 identical**.

**Second: a similarity measure cannot do this job here, and the reason generalises.** The
obvious candidate is the peak signal-to-noise ratio between your page and the reference.
It was measured on this corpus, in the image and independently on the host, agreeing to
four decimal places:

| comparison | PSNR |
| --- | --- |
| a genuine content defect — **one word rendered wrong** | **30.06 dB** |
| the same content with antialiasing turned off — a flag, not a defect | 27.96 dB |
| the same content from a different rasterizer — an engine, not a defect | 28.11 dB |

Read the order. **The real defect came out closer than both benign variations.** The metric
does not merely add noise to the judgement; it **ranks the cases wrongly**, and no
threshold anywhere separates them: one strict enough to catch the wrong word rejects
correct renderings that differ only in antialiasing, and one loose enough to accept those
passes the wrong word.

The mechanism is worth stating, because it is not special to this corpus or to PSNR. A
one-word error perturbs **a few pixels by a lot**; an antialiasing change perturbs **many
pixels by a little**. Averaging the squared difference over the whole page maps those two
very different events onto the same scale — and then inverts them, because the many small
perturbations sum to more than the few large ones. **Any metric that aggregates over the
whole artefact does this.** Exact comparison does not, because it never aggregates: one
wrong pixel is one wrong page.

It is also cheaper by a factor of about fifty. Hashing 300 rendered pages cost 0.76 CPU
seconds; computing PSNR over the same 300 pairs cost 37.8 CPU seconds carefully written,
and 511 CPU seconds written naively. The weaker instrument is the expensive one.

**What this means for you.** Exactness means the frozen flags of §2.1 are
not advice, and it means an implementation that antialiases nearly the same way as the
reference gets a page wrong wherever "nearly" shows. A page is right or wrong as a whole.

## 5. Checking progress

A sample corpus with its expected answers, built from the same reference tools, ships
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

Two notes on running it, both measured, both about cost rather than correctness. Render a
whole document in **one** invocation with `-f 1 -l N`: 300 pages cost 150 CPU seconds that
way against 258 in 300 separate invocations, which re-parse the document each time. And
300 rendered A4 pages at this resolution occupy about 189 MB, so put them under `/tmp` and
delete them.

## 6. Normative documents in this image

* `/app/SPEC/raster-linearize.md` — this milestone's requirements in ISO 32000-1's terms:
  the raster model, shadings, transparency, and Annex F. It also defines the rendering
  interface in full, no man page for that program being available here.
* `/opt/pdfspec/ISO-32000-1-2008.txt` — the published ISO 32000-1:2008 text, the
  normative definition of the file format, the graphics model and the linearized form.
* `/opt/pdfspec/qpdf-running-qpdf.txt` and `/opt/pdfspec/man-qpdf.txt` — the qpdf
  manual's command-line chapter and man page: the option spellings used here and the
  exit status.
* `/opt/pdfspec/man-pdfinfo.txt`, `man-pdftotext.txt` and `man-pdffonts.txt` — the
  earlier poppler-utils interfaces.
* `/app/SPEC/pdf13.md`, `pdf14.md`, `pdf15.md`, `pdf16-17.md`, `pdf20.md`,
  `pdfa1-2.md`, `pdfa3-4-ua.md` — the previous milestones' requirements, all of which
  still apply. `pdf14.md` §5 is the linearized form as a reader sees it.
