## 1. What gets built

| | |
| --- | --- |
| workspace root | `/app` |
| build command the checker runs | `cargo build --release --bin wild --jobs 4` |
| required output | an executable binary target named **`wild`** |
| where the checker looks for it | `$CARGO_TARGET_DIR/release/wild`, then `/app/target/release/wild` |
| toolchain | the image's stable Rust (`rustc 1.98`) |

The package that owns the binary may be called anything; only the *binary
target* name `wild` is fixed. If `--bin wild` does not produce it, the checker
retries once with a plain `cargo build --release` before giving up.

The build must succeed from the workspace's own `Cargo.lock`. The network is
reachable during the run, but a build that depends on resolving a floating
version is a build that can fail for reasons that have nothing to do with your
linker.

`target/` and `.git/` are stripped from the workspace between steps, so the
checker always compiles the source that is actually there.

## 2. How the linker is invoked

`wild` is invoked as a GNU-`ld`-compatible link stage:

```
wild [options] -o OUTPUT INPUT...
```

The synopsis is a shape, not an order: options and inputs may be interleaved,
and `-o` may appear anywhere on the command line. The checker passes `-o` after
the positional inputs.

`INPUT` is any mixture of ELF relocatable objects (`ET_REL`), `ar` archives,
shared objects (`ET_DYN`, from the milestone that introduces them) and linker
scripts. Options the checker uses are introduced by the milestone that introduces
the behaviour, and each one is listed in that milestone's section below. Two
conventions hold throughout:

- **Dash spellings are milestone-dependent, not universal.** From `0.3.0`
  (step 2) onward every long option is accepted with one dash or two --
  `-shared` and `--shared`, `-pie` and `--pie` -- and `-soname NAME`,
  `--soname=NAME` and `--soname NAME` are all accepted, which is what compiler
  drivers emit and what GNU `ld` accepts. At `0.2.0` (step 1) each long option
  is accepted with **exactly one** spelling and which one it is varies by
  option; and the separated `--opt VALUE` form is not general even later.
  `spec/00-compatibility-profile.md` §9 carries the measured tables and
  governs. Accepting both dash spellings everywhere is permitted at every
  milestone, so it is the simplest conforming choice.
- **Options the checker does not name are out of scope**, but an unrecognised
  option must be a diagnosed error, not a silent success.

With no `-m`, the machine of the output is taken from the first input, so an
x86-64 link passes no `-m` at all and a cross link names the emulation
(`aarch64linux`, `elf64lriscv`, `elf64loongarch`; the `-m` values and the
milestone each arrives at are in the step sections below).

The entry point defaults to `_start` at every milestone. The *output path* has
no default at `0.2.0` -- omitting `-o` there is a diagnosed error, not a link to
`a.out` -- and defaults to `a.out` from `0.3.0` onward. The checker always passes
`-o`, at every milestone, so this default is never what is measured.

### Exit status and diagnostics

| outcome | exit status | stderr |
| --- | --- | --- |
| link succeeded | `0` | may be empty |
| link failed | non-zero | at least one line naming what went wrong |

A failed link must **not** be reported as a success: it must exit non-zero, and
it must not leave a **complete, usable** output at the `-o` path for a later
stage to pick up. The checker checks the artifact, never the exit status alone,
precisely because "exited 0 and wrote rubbish" is the failure mode that matters.
Never creating the output, or deleting it once the link is known to have
failed, always satisfies this and is the simplest choice.

What that does **not** say is that a *partially written* file may never survive
a failure. Two measured conditions leave one: at `0.2.0` an undefined symbol
exits non-zero and still leaves a complete-looking `ET_EXEC`, and from `0.7.0`
a relocation whose value does not fit its field is diagnosed only after the
output has been written, so the file stays at the `-o` path at every release
from there on. `00-compatibility-profile.md` §8 carries the measured table,
names both exceptions and the milestone each obligation starts from, and
governs. A case may assert that a rejection left nothing behind, and cases in
this package do for the rejections the linker diagnoses before it writes
anything -- an unrecognised option, an unreadable input, a malformed `@FILE`, a
script parse error -- but no case asserts the artifact's absence for either of
the two conditions above.

Where a milestone below says a diagnostic must *name* something — a symbol, a
relocation type, an input file — that token must appear somewhere on stderr. The
wording around it is yours: no prefix, phrasing or particular exit value is
checked, only that the status is non-zero and the token is present.

**Milestone caveat — exit status at 0.3.0.** The table above holds for step 1's
Oracle (0.2.0). At 0.3.0 some input-error conditions (for example, an undefined
symbol) exit 0 and write an artifact rather than exiting non-zero. This diverges
from the stated contract and is corrected from 0.4.0 onward. Cases at steps 02+
that assert a non-zero exit from a failed link **from an input-semantics
error such as an undefined symbol** must target 0.4.0 or later as their
Oracle, or must avoid that assertion at 0.3.0. Option-diagnosis rejections
(such as a bad `--hash-style` value or a malformed `--build-id` argument)
and other non-input-semantics failures are correctly diagnosed at 0.3.0 and
are in scope without restriction.

## 3. What the checker inspects

Each requirement group is a directory of hermetic cases. A case assembles its own
inputs from text with `clang-16`, links them with your `wild`, and then does one
or both of:

- **Structural inspection** with `llvm-readelf-16 --wide` (`-h -S -l -d -r -s
  --dyn-syms -n -V`, and also `-p`, `--hex-dump` and `--unwind`) over the
  produced file: header fields, section table, program headers, dynamic tags,
  relocation entries, symbol tables, notes, version records, string- and
  hex-dumps of a named section, and unwind tables.
- **Disassembly and section contents** with `llvm-objdump-16` (`-d`, `-s` and
  `--dwarf=frames`). This is part of the in-scope surface, not a development
  aid: the cases read instruction operands out of `-d` output and count
  FDEs out of `--dwarf=frames` output.
- **Execution** of the produced file — natively for x86-64, under
  `qemu-<arch>-static` for the emulated architectures — comparing exact stdout
  and exit status.

A group is **all-or-nothing**: every case in it must pass for the group to
count. Groups come in two kinds, gate and requirement. `tests/groups.tsv`
records which is which, but that file belongs to the checker and is not
shipped in this workspace — so every group this step runs is listed by name
and kind in this step's own section below.

**Gate groups** are mandatory. They add no new requirement: they are proof of
life — your workspace builds with `cargo build --release --bin wild` and the
`wild` it builds starts (an ordinary exit status of any value counts as
starting; only a timeout, a failure to execute or death by a signal does not)
— plus one deterministic end-to-end path for each milestone inherited from an
earlier step, named `gate-m01`, `gate-m02` and so on, one per inherited
milestone. `gate-mN` re-checks milestone N, so what it covers is that
milestone's own "in-scope surface" section below. If any gate group fails, the
step is not satisfied, whatever else passed, and the verifier's
`reward_details.json` names the gate group that failed and why. The gate
corpus belongs to the checker and is not shipped in this workspace, so there
is no file for you to run: reproduce those behaviours yourself before you
hand the worktree on.

**Requirement groups** are this milestone's own new capability, plus its
peripheral breadth. They are **not** uniform: each group covering this
milestone's headline capability and each peripheral one is listed in this
step's section below, which is what to read before you decide what to build
first.

Two consequences are worth internalising. Keeping the earlier milestones
alive is not a requirement in its own right: a submission that preserves
everything and implements nothing new satisfies none of the new work. And
breaking one of them means the step is not satisfied — and the gate corpus is
the checker's, not shipped here, so you cannot run it yourself. Re-check the
earlier milestones' behaviour before you hand the worktree on.

### The fixture shape

Every runnable fixture is freestanding: `-nostdlib -ffreestanding`, a
hand-written `_start` that calls `int entry(void)` and exits with its return
value, and a `sys_write` built on the raw syscall. Nothing links against the
host C library, so the same corpus runs on all four architectures. You will not
be asked to link a glibc program, and no fixture needs a populated sysroot tree
on disk. That is a statement about the fixtures, not about the option:
`--sysroot=DIR` is a `-L` search-path rewrite, not a libc dependency, so "no
sysroot is needed" says nothing about whether the option itself is exercised —
the milestone sections below are what tell you that.

### Available tooling

Inside the image, for your own experiments: `clang-16` (a cross compiler and
assembler for all four target architectures), `lld-16`, GNU `ld` and `binutils`
2.40, `llvm-*-16` binutils, and `qemu-user-static`. Use them freely as reference
oracles while you develop.

## 4. Out of scope

Named explicitly so you do not spend the clock on it:

- **Byte-identity with any other linker.** Section order, padding, string-table
  layout, `.comment`, build-id computation and the choice among legal
  relaxations all differ between `ld`, `lld` and `mold`. Nothing is checked for
  byte equality with another linker's output.
- **Speed.** No wall-clock, throughput or memory comparison is measured at any
  milestone.
- **Output determinism across thread counts.** `spec/00-compatibility-profile.md`
  requires that `--threads=1` and `--threads=8` produce identical files, and you
  should honour it, but no case asserts it. A byte comparison between two links
  at different thread counts is not reproducible enough to check — it can fail
  from concurrent load on the build host rather than from anything your linker
  did — so it was removed rather than left in as a case that fails at random.
- **`--map` / `-Map` / `--print-map`.** No map-file format is in scope.
- **The dynamic loader.** Mapping, `DT_NEEDED` closure, lazy binding,
  `dlopen`/`dlsym` and the TLS runtime are out of scope; the system `ld.so` is
  an interoperability target, not something to reimplement.
- **Incremental linking**, **ICF**, and **`--emit-relocs`-driven kernel module
  linking.**
- **Non-ELF containers** (Mach-O, PE/COFF, WebAssembly), **big-endian output**
  and **32-bit ELF output**.
- **Architectures other than x86-64, AArch64, RISC-V and LoongArch64**, and each
  of those only from the milestone that introduces it.
- **DWARF rewriting** beyond section merging, tombstoning and the index
  construction named at the final milestone.
- **A JSON or textual dump tool.** Structural inspection is the checker's job and
  it uses `llvm-readelf`; you are not asked to ship an inspector.

## 5. Delegation is out of scope

The linker must do the linking. Invoking `ld`, `ld.lld`, `mold`, `gold` or any
other linker from inside `wild` — directly, through a driver, or by handing work
to a library that does it — means the step is not satisfied, regardless of
the output.
Reading another linker's *output* while you develop is fine and encouraged.

## 6. Time budgets

The checker is not patient, and the limits are not otherwise discoverable. They
are the same at every milestone:

| budget | limit |
| --- | --- |
| any single test case | 60 s |
| the whole corpus | 1800 s |
| building your linker | 900 s |
| the built artifact starting up | 60 s |

A case that exceeds its limit is recorded as a failure, not as a slow pass, so
a correct but very slow linker fails the group. The headroom is large -- no
milestone's whole corpus takes a minute against the reference linker -- so
these limits exist to stop a hang, not to encourage micro-optimisation.

## Step 1's in-scope surface

The checker links **x86-64 relocatable objects and `ar` archives** it compiles
itself and inspects, and usually runs, the result.

Milestone 1's requirement groups, as checked at step 01
(§3 explains the group model):

| group | what it covers |
| --- | --- |
| `s01-archives` | Archive extraction driven by the undefined set |
| `s01-reloc-x86-64` | x86-64 relocation arithmetic |
| `s01-static-core` | The static ELF executable: identity, symbol table, segments |
| `s01-symbols` | Symbol resolution: strong, weak, common and local |
| `s01-ehframe` | The `.eh_frame_hdr` binary search table |
| `s01-merge-strings` | Mergeable string deduplication |
| `s01-output-controls` | Output controls: stripping, GC, `-o` and option diagnosis |

Command-line surface exercised at this step:

| option | checker's expectation |
| --- | --- |
| `-o FILE` | output path |
| `-L DIR`, `-l NAME` | library search path and `libNAME.a` lookup |
| `--start-group` / `--end-group` | archive set iterated to a fixpoint |
| `--gc-sections` | unreferenced sections dropped, `SHF_GNU_RETAIN` ones kept. Collection is the **default** at this step (`spec/00` §6): the link that checks the default passes no GC flag, and `--no-gc-sections` is the off-switch |
| `--eh-frame-hdr` | `.eh_frame_hdr` and `PT_GNU_EH_FRAME` emitted |
| `--strip-all` | every `.debug_*` section absent, output smaller than the unstripped link. `.symtab`/`.strtab` are **kept** at this milestone -- 0.2.0 does not remove them, and they first disappear at 0.3.0. No step-1 case reads them after `--strip-all`, so their presence or absence is out of scope either way (`spec/02` §3.2) |
| `--strip-debug` | every `.debug_*` section absent, `.symtab`/`.strtab` **kept** intact, `e_entry` unchanged |
| `--no-string-merge` | mergeable string sections are *not* deduplicated; `.rodata` keeps every copy. Measured at step 01 (`merge-shrinks-rodata`); no case at this milestone re-measures the disable path, and the default path is measured here |

`caselib.sh` builds every fixture's `_start` by hand (`rt_obj`), so no case at
this step passes `-e`/`--entry`; the entry point is exercised only as the
default. The parenthesised short/alias spellings above are cut, not merely
unlisted, because this step's own Oracle (0.2.0) rejects them as unrecognised
arguments -- they are not "the checker does not name them", they are actively
wrong at this milestone. Measured first-accepted tag for each, so a later
agent skimming this file is not misled into thinking they are available early:

| spelling | first accepted at |
| --- | --- |
| `--output FILE` (alias of `-o FILE`) | 0.6.0 |
| `-(` / `-)` (aliases of `--start-group` / `--end-group`) | 0.4.0 |
| `--no-gc-sections` | 0.3.0 |
| `-s` (alias of `--strip-all`), `-S` (alias of `--strip-debug`) | 0.4.0 |
| `--entry=SYM` | 0.5.0 |
| `--entry SYM`, `-e SYM` | 0.6.0 |

Two further options are *accepted* at 0.2.0 and do nothing observable there.
They are called out separately because accepted-and-inert is the trap the
"first accepted at" table above does not catch: a case that passes one of them
links cleanly, and the assertion that looks as though it checks the option in
fact checks nothing. Neither is checked at this step.

| option | at 0.2.0 | first observable effect |
| --- | --- | --- |
| `--build-id[=STYLE]` | accepted, exit 0, **no note section emitted** | 0.3.0 emits `.note.gnu.build-id` |
| `-z noexecstack`, `-z execstack` | accepted, exit 0, **no `PT_GNU_STACK` at all** | 0.3.0 emits `PT_GNU_STACK`, `RW` by default and `RWE` under `-z execstack` |

An unrecognised option is a diagnosed error at every tag from 0.2.0 onward (see
`unrecognized-option-rejected.case` in `s01-output-controls`), so none of the
spellings above may be treated as silently accepted early.

Objects are built with `clang-16 -nostdlib -ffreestanding`; the entry point is a
hand-written `_start` that calls `write(2)` and `exit_group(2)` directly. Nothing
in this step's corpus links against libc or produces a dynamic executable, so a
correct static non-PIE `ET_EXEC` output is the whole target.

What the corpus checks on the output, beyond it running (illustrative; the
requirement lives in `spec/`, and where the two differ the `spec/` text governs):

- `e_ident`, `e_type == ET_EXEC`, `e_machine == EM_X86_64`, `e_entry`;
- `PT_LOAD` segments: ordering by `p_vaddr` and `p_flags` -- never the *number*
  of them, which `spec/00` §2 rule 1 explicitly excludes. `spec/00` §5 also
  requires the congruence `p_vaddr % p_align == p_offset % p_align`; no case
  reads `p_offset`, so it is stated here for completeness and not measured;
- `.text`/`.rodata`/`.bss` addresses, sizes and flags, `.data`'s *size* only,
  and that `.bss` occupies no file space;
- symbol values in `.symtab` for globals the fixture defines;
- that no `R_X86_64_*` relocation survives into the output;
- `.eh_frame_hdr` present, `PT_GNU_EH_FRAME` covering it, and its binary search
  table sorted by initial location -- every case asks for it with
  `--eh-frame-hdr`, because only 0.2.0 synthesises the header from the mere
  presence of `.eh_frame`;
- merged string sections: identical literals from two objects share one address;
- `--gc-sections`: a function reachable only from a dropped section is gone, one
  reachable from `_start` or from a `KEEP`-equivalent root survives. The link
  checked here passes **no** GC flag: the default collects.

Diagnostics that are checked as behaviour, not as text at this step. Each one
must fail the link with a **non-zero exit status**, write its message on
**stderr**, and write **nothing on stdout**:

- an **unrecognised option** -- the stderr message must contain the offending
  option as it was written on the command line, whole token included (so
  `--bogus-option=value` is named as `--bogus-option=value`) (`s01-output-controls`'s three `unrecognized-option-*` cases:
  `-rejected`, `-among-valid-flags`, `-spellings`);
- an **input file that cannot be opened** -- the stderr message must contain
  the path as it was given on the command line
  (`s01-static-core/missing-input-file-diagnosed.case`). A file that opens but
  is badly formed need *not* name the path at this step; only `0.10.0` does
  that, so the corpus asserts a non-empty stderr and nothing more
  (`s01-archives/malformed-archive-rejected.case`,
  `s01-static-core/elf-ident-rejected.case`,
  `s01-reloc-x86-64/unknown-relocation-type-diagnosed.case`). See `spec/00`
  §8, which separates the two conditions.

**Explicitly out of this step's in-scope surface.** Each of the following is a
stated requirement somewhere under `spec/`, and none of it is in scope here,
because the reference's behaviour changes across the tags this step's corpus
must pass at (`0.2.0` through `0.10.0`) and no assertion can hold across all
nine:

- *Duplicate-strong-definition detection.* The reference links a duplicate
  strong definition successfully through `0.4.0` and starts diagnosing it at
  `0.5.0`. Out of scope here.
- *Which* strong definition wins (`spec/02` §3's first-definition rule). It is
  only observable where the link succeeds, which is `0.2.0`-`0.4.0` only, so
  no case in an `s01-` group can assert it. Checked from the milestone that
  introduces the diagnostic.
- *Visibility monotonicity across objects* (`spec/02` §1.1): a hidden
  reference meeting a default definition. The reference does not enforce this
  until `0.9.0`. What *is* checked here is narrower -- that a definition
  carrying a hidden attribute keeps `STV_HIDDEN` in the output
  (`s01-symbols/visibility-lattice.case`).
- *Relocation-overflow detection* on `R_X86_64_32`/`R_X86_64_32S`. Not
  performed until `0.7.0`.
- *Tail merging* of string literals -- a string that is a proper suffix of
  another may be kept as a separate copy (`spec/04` §3).

See `spec/02-symbols-and-archives.md` and `spec/04-layout-and-sections.md` for
the milestone-scoped wording.
## Step 2's in-scope surface

Everything step 1 checked still applies, and the checker now also builds **shared
objects and position-independent executables**, and links against them.

Milestone 2's requirement groups, as checked at step 02
(§3 explains the group model):

| group | what it covers |
| --- | --- |
| `s02-debug-info` | DWARF sections, tombstones and compressed input |
| `s02-pie-relative` | Position-independent output and `PT_INTERP` |
| `s02-plt-got` | GOT slots, PLT stubs and eager binding |
| `s02-shared-output` | Shared-object output and the dynamic table |
| `s02-notes` | Note sections and their merge rules |
| `s02-symbols` | Symbol-table passthrough into dynamic-capable output |
| `s02-version-script` | Export filtering from a version script |

| option | checker's expectation |
| --- | --- |
| `-shared` | `ET_DYN` output with `.dynsym`, `.dynstr`, `PT_DYNAMIC` |
| (default) / `-pie`, `--pie` | `ET_EXEC` / `ET_DYN` with `DF_1_PIE` set in `DT_FLAGS_1` |
| `-soname NAME` | `DT_SONAME` |
| `--dynamic-linker PATH` | `PT_INTERP` with exactly that string |
| `-rpath DIR` | `DT_RPATH` / `DT_RUNPATH` |
| `-z now`, `-z lazy` | eager binding either way (see below); `-z now` additionally sets `DF_BIND_NOW`/`DF_1_NOW` |
| `--version-script FILE` | `local:` symbols absent from `.dynsym` |
| `--hash-style=gnu`, `--hash-style=both` | accepted; `.gnu.hash` and `DT_GNU_HASH` present either way |
| `--hash-style=NONSENSE` | non-zero exit, a diagnostic naming the rejected value, and no output file left behind |
| `--no-gc-sections` | disable dead-section elimination; all input sections retained regardless of reachability |
| `--build-id[=STYLE]` | nothing emitted unless the option is given; `=0x<hex>` makes the descriptor those bytes verbatim in a `NT_GNU_BUILD_ID` note |
| `--build-id=0x<odd-or-non-hex>`, `--build-id=UNKNOWNSTYLE` | non-zero exit, a diagnostic naming the rejected value, and no output file left behind |

The reference implementation is always eagerly bound, whether or not `-z now`
is given: cross-module calls go through a `.plt.got`-style stub over a GOT
slot filled by an `R_X86_64_GLOB_DAT` relocation in `.rela.dyn`, never a
lazily-bound classic PLT. A correct linker at this step therefore has **no**
`.rela.plt`, `DT_JMPREL`, `DT_PLTRELSZ` or `R_X86_64_JUMP_SLOT` in its output;
a case may assert their absence as well as the `GLOB_DAT`/`.plt.got` presence.
`--no-pie`, `-h`, `-I`, `-R` and `--enable-new-dtags` are not recognised
arguments at this milestone and are out of scope here. `--disable-new-dtags` is
accepted but inert, and is out of scope too. An unresolved undefined symbol in
a `-pie` or default-executable link is **not** diagnosed at this milestone, so
nothing here checks that.

Checked on dynamic output: the `DT_*` tag set and its consistency
(`DT_RELA`/`DT_RELASZ`/`DT_RELAENT` agreeing with the section they describe,
`DT_PLTGOT` agreeing with `.got` (the reference does not split the GOT; a split-GOT layout where `DT_PLTGOT` points at `.got.plt` is equally conforming but out of scope at this milestone)); `.rela.dyn` containing `R_X86_64_RELATIVE`
for each absolute slot in `-pie`/`-shared` output and **no** absolute
`R_X86_64_64` against a preemptible symbol; the `.plt.got`-style stub's
alignment (16) and entry size (16); `PT_NOTE` covering every `.note.*` section
with correct note-record framing. No SysV `.hash` table is checked: at this
milestone the reference emits `.gnu.hash` alone — no `.hash` section, no
`DT_HASH` — and a link MUST NOT fail merely because `.hash` is absent. (A later
milestone starts emitting `.hash` alongside `.gnu.hash`; that is that
milestone's business, and nothing at step 2 asserts either way. Correspondingly
`--hash-style=sysv` is *not* checked here, because the reference rejects it at
this milestone and accepts it later; only `gnu` and `both` are.)

Symbol-table passthrough into dynamic-capable output is checked separately from
the dynamic tables themselves: an `SHN_ABS` symbol defined with `.set`/`.equ`
keeps its numeric value byte for byte through the link, whatever addresses the
sections around it end up at, and remains readable both from the symbol table
and from the running program.

Debug info is checked by relocating `.debug_info` against a discarded target and
requiring the documented tombstone value, and by linking objects whose
`.debug_str` was `SHF_COMPRESSED` on input.
## Step 3's in-scope surface

The corpus now contains **AArch64** objects, which are built with
`clang-16 -target aarch64-unknown-linux-gnu` and run under
`qemu-aarch64-static`. `-m aarch64linux` is passed on AArch64 links. Adding a
backend must not move any x86-64 result: each earlier milestone is re-checked
here by its own `gate-m*` group, and any gate failure means the step is not
satisfied (§3).
Those groups are one end-to-end path per milestone, **not** the earlier steps'
full corpora, so keeping x86-64 working is your responsibility well beyond what
they sample.

Two statements in *Step 1's in-scope surface* above are scoped to that milestone
and no longer hold here: the Oracle named there (`0.2.0`) is step 1's, not this
step's, and this step's corpus **does** produce dynamic executables -- two of
them are run under the real loader (`-dynamic-linker
/lib64/ld-linux-x86-64.so.2` with `-rpath '$ORIGIN'`), which is how the copy
relocation and the version records are checked by something that depends on
them.

Milestone 3's requirement groups, as checked at step 03
(§3 explains the group model):

| group | what it covers |
| --- | --- |
| `s03-aarch64` | the AArch64 backend and its relocations |
| `s03-tls` | `PT_TLS`, the four access models, TLSDESC |
| `s03-versioning` | `.gnu.version`, `.gnu.version_r`, `name@V` binding |
| `s03-relro-copyrel` | `PT_GNU_RELRO` and copy relocations |
| `s03-scope-controls` | `--whole-archive`, `--sysroot=`, `-z nocopyreloc`, `__start_`/`__stop_`, and the two hard errors |

| option | checker's expectation |
| --- | --- |
| `-m EMULATION` | selects the output architecture |
| `-z now` | eager binding with `DF_BIND_NOW` in `DT_FLAGS` and `DF_1_NOW` in `DT_FLAGS_1`; inherited from step 2 (`spec/05-dynamic-linking.md` §6.1), and passed by `s03-relro-copyrel` alongside `-z relro` |
| `-z relro` | `PT_GNU_RELRO` emitted, covering the slice described below |
| `-z norelro` | accepted, no diagnostic — but no case at this milestone passes it, so acceptance is permitted-and-unmeasured here. The reference emits `PT_GNU_RELRO` either way, so **suppressing the segment is permitted and out of scope here** (`05-dynamic-linking.md` §13.1); it becomes a requirement at the script-driven-layout milestone |
| `-z nocopyreloc` | a copy relocation becomes a link error instead |
| `--whole-archive` / `--no-whole-archive` | every member extracted / lazily |
| `--sysroot=DIR` | a `-L` search path whose first character is `=` is resolved under `DIR`: `-L=/usr/lib` with `--sysroot=/tmp/root` searches `/tmp/root/usr/lib`, and without the option that same `-L` finds nothing. **Checked in the `=`-joined spelling only** — this is the exception `00-compatibility-profile.md` §9 allows a milestone section to make; no case passes the separated `--sysroot DIR` |

AArch64 relocation checks read the instruction encodings back with
`llvm-objdump-16 -d`: an `adrp`/`add` pair must address the right page and page
offset, a `b` or `bl` out of ±128 MiB (both take `R_AARCH64_JUMP26`) must be
diagnosed with **the relocation type** and **the symbol** named on stderr and
nothing else about the message checked (`07-aarch64-relocations.md` §3) — the
type token is matched case-insensitively and the `R_AARCH64_` prefix is not
required, so `JUMP26`, `jump26` and `R_AARCH64_JUMP26` all satisfy it — and
`ldr`/`str` low-12 relocations must be
scaled by access width. `e_ident` aside, `e_flags` MUST be written as `0`:
the AArch64 LP64 ABI defines no processor-specific flag bits
(`07-aarch64-relocations.md` §12.1). On the dynamic side the same pointer
initialiser that must stay static-clean in a non-PIE becomes, in a `-shared`
link, exactly one `R_AARCH64_RELATIVE` when its target cannot be preempted,
and a GOT-mediated reference to a preemptible data symbol becomes exactly one
`R_AARCH64_GLOB_DAT` naming it (`07-aarch64-relocations.md` §10).
`R_AARCH64_JUMP_SLOT` is **not** checked: this linker binds eagerly and
`05-dynamic-linking.md` §8 requires no case to produce one.
`STO_AARCH64_VARIANT_PCS` — bit `0x80` of `st_other`,
set by the `.variant_pcs` directive — must survive into both `.symtab` and
`.dynsym`. The companion `DT_AARCH64_VARIANT_PCS` tag is
**not** checked, for the reason `07-aarch64-relocations.md` §12.2 gives: a linker
that always binds eagerly and emits a plain PLT does not need it, and this one
always binds eagerly.

TLS is checked in all four models and on both architectures, but not every
model on both: **local dynamic is checked on x86-64 only**, for the same
toolchain reason that leaves traditional AArch64 general dynamic out of scope
below. A correct AArch64 local-dynamic implementation is still required by
`08-thread-local-storage.md` §5; nothing in this image can build an input that
exercises it. `PT_TLS` must have
`p_memsz` covering `.tdata` plus `.tbss`, and `p_filesz` at least the size of
`.tdata` and no larger than `p_memsz`; the reference emits
`p_filesz == p_memsz`, and the strict `p_filesz == size_of(.tdata)` form is
**not** checked
(`08-thread-local-storage.md` §2). The initial-exec and local-exec sequences
are checked by running the program and reading the thread-local back — on x86-64
as well as on AArch64, which is what checks the variant-II rule that every
offset from TP is **negative** (§3) — so the IE→LE and LD→LE relaxations
are held to their effect and not to their bytes; **no case asserts the rewritten
bytes** of §7.1 or §7.2, so the GD→LE and GD→IE rewrites are required of a
correct linker and out of scope at this milestone. Local dynamic is checked by the
single shared module descriptor §5.2 requires: two LD variables in one shared
object must produce exactly one `R_X86_64_DTPMOD64` and no `DTPOFF64` beside
it. TLSDESC is checked in its unrelaxed form through `R_X86_64_TLSDESC` and
`R_AARCH64_TLSDESC` entries in `.rela.dyn`. The traditional, non-descriptor
AArch64 general-dynamic pair (`R_AARCH64_TLS_DTPMOD64` / `_DTPREL64`) is **not**
checked: `clang-16` has no `-mtls-dialect` switch for AArch64 and its assembler
rejects the `:tlsgd:` relocation specifiers, so nothing in this image can build
an input that asks for them.

Versioning is checked as a *consumer* of versions, not a producer of them.
`.gnu.version` is one `Elf64_Half` per `.dynsym` entry; `.gnu.version_r`
records must chain correctly through `vn_next` and `vna_name` into `.dynstr`,
and each `Vernaux` must carry the System V ELF hash of
`09-symbol-versioning.md` §6 in `vna_hash` — `llvm-readelf -V` does not print
that field, so the checker reads the section's bytes with
`llvm-readelf-16 --hex-dump=.gnu.version_r`. `name@@version` must appear as a
default definition and `name@version` as a non-default one, and one versioned
executable is **run** under the real loader, so the records are matched by
something that depends on them. Emitting `.gnu.version_d` is **not** required
and **not** checked here: it belongs to the milestone that introduces version
*definitions* (`09-symbol-versioning.md` §4), and at this one a linker only
consumes the verdefs its inputs carry. Requesting a version a dependency does
not define is the SHOULD of `09-symbol-versioning.md` §8, not a MUST here: the
reference drops the reference and exits successfully at this milestone, so
**neither a diagnostic nor a non-zero exit status is checked** for it.
Implementing the check is permitted and has no effect. `DT_VERNEEDNUM` counts
*versioned* dependencies, not dependencies: an unversioned shared object that
is genuinely referenced gets its `DT_NEEDED` and no `.gnu.version_r` record at
all, and must not move the count (`09-symbol-versioning.md` §5, §7).

`PT_GNU_RELRO` must cover `.dynamic` and `.got` and, under `-z now`, `.got.plt`,
and must start where the writable `PT_LOAD` containing it starts. What is checked
is the **partition**, not an alignment: no allocated writable section may
straddle the end of the window, and `.data` and `.bss` must lie wholly after it.
The loader rounds the end down to a page boundary itself, so neither end of the
segment is required to be page-aligned and neither alignment is checked
(`05-dynamic-linking.md` §13.1).

Copy relocations are checked in both directions. The case `05-dynamic-linking.md`
§13.2 describes — a direct reference from non-writable text in a non-PIC
executable to an `STT_OBJECT` defined by a shared library — must produce
exactly one `R_X86_64_COPY` into `.bss`, redefine the symbol there with the
size taken from the library, and emit it into both `.symtab` and `.dynsym`;
that executable is then **run** under the real loader, which is the only check
that the reservation is actually usable. The three prohibitions of the same
section are checked as well: no copy relocation for a function, for `-shared` or
`-pie` output, or for a reference from a writable section. §13.2's parenthetical
-- such a reference *can* use a `GLOB_DAT`-class slot instead -- is permissive,
so **which fallback you choose is out of scope**. What is checked is that no
`R_X86_64_COPY` appears, that the symbol is not redefined in the output (it
stays `UND` in `.dynsym`, i.e. step 2 of the recipe does not happen), and that
a dynamic relocation naming it is still emitted, so the reference reaches the
loader instead of being baked into the image.

Two link failures are hard errors from this milestone on, and in both the only
checked things are the non-zero exit status and one named token on stderr.

An **undefined symbol** must fail the link and stderr must name the symbol.
The word "undefined" is *not* checked, and naming the referencing object as well
is good practice but is not required either
(`00-compatibility-profile.md` §8).

An input built for **another architecture** must fail the link and stderr must
name the offending file (`00-compatibility-profile.md` §8,
`07-aarch64-relocations.md` §12.1). Nothing about the architecture name, the
word "incompatible" or any other phrasing is checked.

As everywhere in this contract, no prefix, phrasing or particular exit value is
checked — only that the status is non-zero and the named token is present.
## Step 4's in-scope surface

The checker now passes **linker scripts** with `-T FILE` and expects them to
override built-in layout.

| option | checker's expectation |
| --- | --- |
| `-T FILE` / `--script=FILE` / `-script=FILE` | `SECTIONS` drives output layout; the three spellings are exact synonyms and MUST produce byte-identical output for the same inputs |
| `--start-lib` / `--end-lib` | enclosed objects behave as archive members |
| `-z origin` | `DF_ORIGIN` and `DF_1_ORIGIN` |
| `-l:libname.a` | exact-filename library lookup |
| `--entry=SYM` | overrides a script's `ENTRY(...)` and the default entry point; only this attached spelling is accepted at this milestone -- `--entry SYM` and `-e SYM` are unrecognised arguments until step 5 (*Step 1's in-scope surface* above records the measured first-accepted tag for each) |
| `--undefined=SYM` (`-u SYM`, `-uSYM`) | forces an archive member in; a `-u` name that nothing defines is **not** an error and the link exits 0 |

Script constructs exercised at this step: output-section descriptions with a
header `ALIGN(dec)` written **after the colon** (`.name : ALIGN(64) { ... }` —
the pre-colon spelling `.name ALIGN(64) :` is a parse error at this milestone,
see *Linker scripts* §4), input wildcards including `*(.text .text.*)`,
`. = ADDR;` between output-section descriptions and
`sym = .;` inside one, `KEEP(...)` surviving garbage collection, `ENTRY`, and a
script whose output-section order differs from the built-in one — where the
two disagree, the bucket rule (RO → X → RELRO → W) wins over the script's
listed order for descriptions that carry no explicit address.

Note that **section GC is on by default**: a link with no `--gc-sections` flag
sweeps unreferenced sections, and `--no-gc-sections` is what turns the sweep
off. Assertions about GC at this step are written against all three forms.

Three script constructs are **not** in scope at this step, because the reference
linker does not accept them at this milestone:

- the in-header address form `.name 0x200000 : { ... }` — a parse error here;
  `. = 0x200000;` ahead of the description is the spelling that works, and the
  in-header slot arrives at step 8;
- a symbol assignment whose right-hand side is anything other than a bare `.`
  (so `sym = expr;` overriding an object's definition is out of scope), and
  `. = expr;` *inside* an output-section description;
- the `?`, `[abc]` and `[a-z]` glob metacharacters in a section or file
  pattern. Only `*` is exercised at this step; the character-class and
  single-character forms arrive at step 8;
- the **per-file** input-section description `FILE(pat)` (`a.o(.text)`,
  `*a.o(.text)`). It is a parse error at this milestone -- measured a parse
  error at 0.5.0 through 0.8.0 and first parsed at 0.9.0 -- so it too
  arrives at step 8, and `*(pat ...)` is the only input statement form
  exercised here. *Linker scripts* §5 lists the form because the grammar
  section is shared across milestones; §13 is what says when it is due. No case at any
  milestone supplies a `FILE(...)` statement, so its *behaviour* is nowhere
  checked even though it must parse.

One thing *Linker scripts* §1 states about this milestone is deliberately
**not** checked: that the attached `-TFILE` and the space-separated
`--script FILE` spellings are rejected here. They are accepted from step 5
on, so a case asserting that they fail would pass only at this step's own
Oracle and would then reject a build that accepts a spelling every
later milestone requires. No case in scope supplies either spelling, and
accepting them has no effect at this step.

See *Linker scripts* §13 for the per-milestone construct table. The checker reads back
section addresses with `llvm-readelf-16 -S --wide` and symbol values with
`llvm-readelf-16 --syms`, and runs the program where the layout leaves it
runnable.

Thin archives (`llvm-ar-16 rcsT`) must resolve their members relative to the
archive's own path. A COMDAT group (`SHT_GROUP`, `GRP_COMDAT`) appearing in two
objects must contribute exactly one copy of every section in the group, and the
discarded copy's symbols must not appear in the output.

Two objects that each define the same strong (non-weak, non-COMDAT) symbol
must now fail the link: stderr must name `Duplicate symbols detected`, the
symbol, and both defining objects. Through step 3 the reference silently keeps
the first definition; this is a step-4 capability, not an inherited one, and
COMDAT is the mechanism that legitimately avoids the clash.

Shared objects built with `--version-script` must now emit `.gnu.version_d`
records for the versions they define, not merely hide the locals.

Protected visibility in a shared object must be resolved without a copy
relocation and without a PLT indirection in the defining object: no dynamic
relocation in that object may **name** a protected symbol, because naming it
is a run-time lookup and a protected symbol is not preemptible inside its own
component. A symbol-less `R_X86_64_RELATIVE` is not such a naming and is
explicitly permitted -- a linker that relaxes a GOT-relative reference to a
direct one MAY keep the now-unused GOT slot (*x86-64 relocations* §4.2,
"keeping it is harmless"), and the slot needs that `RELATIVE`. The checker
counts dynamic relocations other than `R_X86_64_RELATIVE`, not all of them.

Milestone 4's requirement groups, as checked at step 04
(§3 explains the group model):

| group | what it covers |
| --- | --- |
| `s04-linker-scripts` | `SECTIONS` linker scripts driving output layout |
| `s04-archives-and-comdat` | `--start-lib`, thin archives, `-l:`, `-u`, COMDAT dedup |
| `s04-versiondef-and-protected` | version definitions and protected visibility |

## Step 5's in-scope surface

The corpus gains **RISC-V** objects (`clang-16 -target riscv64-unknown-linux-gnu`,
run under `qemu-riscv64-static`, linked with `-m elf64lriscv`), and the
version-script grammar is exercised in full. Adding a third backend must not
move any x86-64 or AArch64 result: each earlier milestone is re-checked here
by its own `gate-m*` group, and any gate failure means the step is not
satisfied (§ 3). Those
groups are one end-to-end path per milestone, **not** the earlier steps' full
corpora — the four of them together run 4 cases, against the 26 cases in scope of
this step's own six groups — so keeping x86-64 and AArch64 working is your
responsibility well beyond what they sample.

Milestone 5's requirement groups, as checked at step 05
(§3 explains the group model):

| group | what it covers |
| --- | --- |
| `s05-crel` | CREL compact relocation sections |
| `s05-riscv` | RISC-V 64 backend, including script-driven layout on that target |
| `s05-version-globs` | Version script globs and extern blocks |
| `s05-symbol-policy` | Symbol resolution policy: wrap, muldefs, bsymbolic, exclude-libs, defs/undefs, interpose |
| `s05-tls-placement` | Custom `SHT_NOBITS` TLS section placement in `PT_TLS` |
| `s05-unresolved-symbols` | `--unresolved-symbols=` policy and severity |

| option | checker's expectation |
| --- | --- |
| `--wrap SYM` (also spelled `--wrap=SYM`) | references to `SYM` go to `__wrap_SYM`, references to `__real_SYM` go to the original `SYM`. Both are *renames of references*, not new definitions: `SYM` keeps its own definition, `__real_SYM` MUST NOT appear in the output symbol table, and a name that merely begins with `SYM` (`foot` when `foo` is wrapped) is untouched |
| `--allow-multiple-definition`, `-z muldefs` | first definition wins, no error |
| `--unresolved-symbols=VALUE` | see semantics below |
| `--warn-unresolved-symbols`, `--error-unresolved-symbols` | severity of the above; see below |
| `-z defs` / `-z undefs` | undefined symbols in `-shared` rejected / allowed; the last of the two on the command line decides |
| `-Bsymbolic`, `-Bsymbolic-functions`, `-Bsymbolic-non-weak` | binding scope, checked as *which of the shared object's own definitions still need a dynamic relocation*: `-Bsymbolic` binds all of them internally and leaves none; `-Bsymbolic-functions` binds only function definitions, so a data definition keeps its `GLOB_DAT`; `-Bsymbolic-non-weak` binds only non-weak definitions, so a weak function keeps its relocation. None of the three changes which names are exported, and none sets `DF_SYMBOLIC` |
| `--exclude-libs ALL` | archive-supplied symbols hidden from `.dynsym` |
| `--no-relax` | accepted, and a no-op at this milestone: RISC-V relaxation belongs to a later one, so the link must succeed and produce the same output it produces without the option (`11-riscv-relocations.md` § 5). The checker passes it on RISC-V links |
| `-z interpose` | sets `DF_1_INTERPOSE` (bit `0x400`) in `DT_FLAGS_1`, composing with (not replacing) any other `-z` flag already set; absent by default. A plain `-shared` link MUST carry `DT_FLAGS_1 = 0x1` (`DF_1_NOW`, §6 of the dynamic-linking document makes every dynamic link `BIND_NOW`), and the same link with `-z interpose` MUST have `0x401` (`DF_1_NOW | DF_1_INTERPOSE`) |

`--unresolved-symbols=VALUE` selects what an undefined reference from a regular
object file does to the link, independently of the severity flags below:

| Value | Effect on an object-file reference |
| --- | --- |
| `report-all` (the default) | fatal: the link fails, naming the symbol and the referencing object |
| `ignore-all` | the reference is left unresolved (resolves to address zero); the link still produces a complete output |
| `ignore-in-object-files` | same as `ignore-all` for this checker's fixtures, which only ever reference from a plain object file, never a shared library |
| `ignore-in-shared-libs` | narrower than `ignore-all`: an object-file reference is still reported (same as `report-all`); only a reference coming from inside a `-shared` dependency would be ignored, which this step does not exercise |

`--warn-unresolved-symbols` and `--error-unresolved-symbols` change the
*severity* of whatever `--unresolved-symbols=` would otherwise do, without
changing which references are reported: `--warn-unresolved-symbols` downgrades
a would-be-fatal report to a non-fatal diagnostic on stderr and lets the link
finish; `--error-unresolved-symbols` is the explicit spelling of the default
severity. An *ignoring* `--unresolved-symbols=` value silences the report
entirely, and a later severity flag does not resurrect it — the reporting
policy decides whether anything is said at all, the severity flag only decides
how loud it is when something is said.

RISC-V checks read `R_RISCV_*` entries and disassembled `auipc`/`jalr` and
`auipc`/`addi` pairs; `e_flags` must carry the input objects' ABI bits and a
mismatch must be a link error. Two rules of the RISC-V document are stated but
not measured at this milestone, because no case feeds them: the MUST-reject
list of relocation types of §3 (`R_RISCV_RVC_LUI`, `TPREL_I`, `TPREL_S`,
`IRELATIVE`) and the "other high part paired with a LO12 is an error" rule of
§4. Implementing them is conforming and has no effect; not implementing them
loses nothing here. CREL input sections (`SHT_CREL`) must link to the
same output as the equivalent `SHT_RELA` input, byte for byte. Note that the
assembler in this image cannot emit CREL — `clang-16 -Wa,--crel` is rejected as an
unsupported argument — so the checker builds its CREL fixtures by re-encoding a
`SHT_RELA` section into `SHT_CREL` itself, and compares the two links. You need
only read CREL; you are never asked to produce it. CREL is a **container
format, not a RISC-V feature**: it is specified in the RISC-V document only
because RV64 is where this benchmark first needed it, the CREL fixtures used here
are **x86-64** objects, and `SHT_CREL` input MUST be accepted on every target
the linker supports (`11-riscv-relocations.md` § 9). Wiring CREL decoding into
the RISC-V backend alone fails this group.

Version-script grammar: `global:`/`local:` blocks, `*` and `?` globs and
character classes, `extern "C" { ... }` and `extern "C++" { ... }` matching
demangled names (the demangled spelling carries the parameter types and the
namespace qualifiers but **not** the return type: pattern `ns::f(int)` matches
`_ZN2ns1fEi`), and several version nodes in one script with inheritance. The
checker checks which names reach `.dynsym` and with which version index.

TLS addresses must be computed from the `PT_TLS` segment, so a custom
`SHT_NOBITS` TLS section placed by a script must still get the right offset,
and `PT_TLS` must reach the **end of the last** `SHF_TLS` section, not merely
the end of `.tbss`.

Linker scripts gain two spellings at this step — the attached `-TFILE` and the
space-separated `--script FILE` — which MUST mean exactly what `-T FILE`
already meant (`10-linker-scripts.md` § 1, § 13 row 5); the checker links the
same script three ways and compares the bytes. A `SECTIONS` script must also
compose with the other two things this step adds: it must drive layout on a
RISC-V link, and it must combine with a `--version-script` without either one
disturbing the other.
## Step 6's in-scope surface

Versioning now arrives from **inputs** rather than only from the command line,
and the linker gains a second platform flavour of x86-64.

Milestone 6's requirement groups, as checked at step 06
(§3 explains the group model):

| group | what it covers |
| --- | --- |
| `s06-illumos-platform` | The Illumos/Solaris emulation: `-m` elf_x86_64_sol2, `EI_OSABI` and the interpreter |
| `s06-script-version-and-quoting` | `VERSION` blocks and quoted tokens in linker scripts |
| `s06-symver-directives` | `.symver` directives from input objects |
| `s06-entry-points` | Entry points on shared objects and from absolute symbols |
| `s06-file-supplied-arguments` | @FILE argument files and `--retain-symbols-file` |
| `s06-merging-and-limits` | String merging, alignment and 32-bit relocation limits |

| option | checker's expectation |
| --- | --- |
| `--retain-symbols-file FILE` | `.symtab` limited to the listed names |
| `@FILE` | arguments read from a file, mixed with ordinary arguments |
| `-m elf_x86_64` | x86-64, no other effect |
| `-m elf_x86_64_sol2` | x86-64 Illumos/Solaris flavour: supplies the default interpreter `/lib/amd64/ld.so.1` |
| `--dynamic-linker PATH` | sets `PT_INTERP` to `PATH` |
| `--no-dynamic-linker` | clears any interpreter path already set |
| `-e NAME` | entry point; the separated spelling arrives with this milestone (`0.6.0`) and is used here (`spec/00-compatibility-profile.md` §7) |

Checks at this step:

- `.symver` directives assembled into the input object (`asm(".symver ...")` or
  a `.s` fixture) must produce the same `.gnu.version_d`/`.dynsym` result as the
  equivalent version script, including `name@@version` defaults, `name@version`
  aliases that coexist with the base name, and the `name@@@version` spelling,
  which renames rather than aliases;
- a `VERSION { ... }` block **inside** a `-T` script must behave exactly as
  `--version-script` would;
- mergeable string sections: a symbol defined part way into a mergeable string
  must resolve to that interior address and still read back as a valid string
  at run time, and duplicate literals from two objects must end up at one
  address. Which *further* strings a linker chooses to overlap is its own
  business and is out of scope;
- an input section with `sh_addralign` up to and including 65536 must be
  accepted at this milestone — this raises the maximum stated in
  `spec/00-compatibility-profile.md` §5, as §1 of that document allows — and
  the `PT_LOAD` carrying it takes that alignment as its `p_align`;
- `.note.stapsdt` must survive into the output with its note framing intact and
  its embedded addresses relocated, with `sh_addr` 0 and `sh_addralign` 4
  because it is not `SHF_ALLOC`; this must hold with `--gc-sections` in effect
  as much as without it -- from this milestone every `SHT_NOTE` input section is
  a garbage-collection root, which is what keeps the probe site alive when the
  only reference to it is the note's own relocation
  (`spec/06-debug-info-and-notes.md` §11) -- and an unrecognised note section
  must be carried through just the same;
- every 32-bit relocation type must be range-checked: a `R_X86_64_32S` whose
  value does not fit is a link error naming the relocation and the symbol
  (`spec/03-x86-64-relocations.md` §2.2);
- double-quoted names in a script (`"my.section"`, `"sym name"`) must parse;
- a shared object may carry an entry point, including one whose value comes from
  an absolute (`SHN_ABS`) symbol;
- the Illumos flavour is a command-line matter only: `-m elf_x86_64_sol2`
  supplies `/lib/amd64/ld.so.1` as the interpreter when nothing else has,
  an explicit `--dynamic-linker` wins over it in either argument order,
  `--no-dynamic-linker` clears it, `e_ident[EI_OSABI]` and `EI_ABIVERSION` stay
  `0` on output under every emulation, an input object's `EI_OSABI` never
  decides whether it can be linked, and an `-m` value outside the accepted set
  fails the link with the rejected name on stderr;
- the exact syntax of `@FILE` and `--retain-symbols-file` is checked: see
  `spec/13-symver-and-platforms.md` §3 and §4 for quoting, escaping, recursion,
  the absence of any comment syntax, and the forms that must be rejected. Each
  rejected form is checked on exactly three things: a non-zero status, at least
  one line on **stderr** and nothing on stdout, and no file left at the `-o`
  path -- these rejections are diagnosed before the output is written, so §2's
  partial-file exceptions do not reach them. The wording of the diagnostic is
  out of scope.
## Step 7's in-scope surface

The corpus gains **LoongArch64** objects
(`clang-16 -target loongarch64-unknown-linux-gnu`, run under
`qemu-loongarch64-static`, linked with `-m elf64loongarch`). Note that
`ld.lld-16` cannot link these objects; where a reference comparison is wanted the
checker compares against the linker under test's own earlier output, not against lld.

Milestone 7's requirement groups, as checked at step 07
(§3 explains the group model):

| group | what it covers |
| --- | --- |
| `s07-defsym` | `--defsym` symbol definitions |
| `s07-loongarch64` | the LoongArch64 backend |
| `s07-merge-nonstring` | non-string `SHF_MERGE` section merging |
| `s07-gnu-property` | `PT_GNU_PROPERTY` and the x86 ISA-level properties |
| `s07-init-array-order` | `.init_array` / `.fini_array` priority ordering |
| `s07-layout-controls` | page size, stack size, section placement and hash style |
| `s07-variant-pcs` | the `VARIANT_PCS` / `VARIANT_CC` dynamic tags |
| `s07-version-glob-negation` | negated glob character classes in version scripts |

| option | checker's expectation |
| --- | --- |
| `--defsym SYM=EXPR` | a literal or an arithmetic result gives an `SHN_ABS` symbol; a bare symbol reference gives a symbol copying the target's value **and** its defining section index |
| `--hash-style=sysv` | `.hash` and `DT_HASH`; no `.gnu.hash` |
| `--hash-style=gnu` | `.gnu.hash` and `DT_GNU_HASH`; no `.hash` |
| `--hash-style=both` | both `.hash` and `.gnu.hash`, both dynamic tags |
| `--section-start .sec=ADDR` | that section's `sh_addr` |
| `-z max-page-size=N` | `PT_LOAD` `p_align` and the congruence rule |
| `-z stack-size=N` | `PT_GNU_STACK` `p_memsz` |
| `-z x86-64-v2` / `-v3` / `-v4` | the level OR-ed into `GNU_PROPERTY_X86_ISA_1_NEEDED` |

`-z max-page-size=N` raises the page size for this link: every `PT_LOAD` must
then carry `p_align` `N` and still satisfy `p_offset ≡ p_vaddr (mod N)`.
`p_align` is the property checked, not the resulting addresses: `00-compatibility-profile.md` §5 grants a linker either padding the file to a
whole page or advancing the virtual address, so whether a section moves is not
asserted. That section's table entry naming `0x1000` as the `p_align` of every
`PT_LOAD` describes the default with no `-z max-page-size`; from this milestone
the switch overrides it.

`--hash-style=sysv` is checked as a **success** that emits `.hash`/`DT_HASH` and
no `.gnu.hash`; `=gnu` is a success that emits `.gnu.hash`/`DT_GNU_HASH` and no
`.hash`; `=both` emits all four. (`spec/05-dynamic-linking.md`'s Provenance
item 2 and §3 describe an earlier milestone at which `sysv` is a diagnosed
error and no case needs `.hash`. That is superseded from this step; where that
document and this contract disagree about what is checked, this contract is the
authority.)

`--wild-experimental-sframe` and `.sframe` output are **out of scope at this
step**: the step-7 Oracle (`wild-0.8.0`) rejects the option as unrecognised, so
no case can pass it and still succeed, and none does. The option is first
accepted one milestone later, so nothing about `.sframe` is checked before then;
`spec/15-sframe-and-properties.md`'s format description carries forward
unchanged for the milestone that does check it.

`PT_GNU_PROPERTY` checks: `.note.gnu.property` records are combined per property
type, by that type's class, and the note payload is 8-byte aligned on ELF64.
The three classes are checked as `15-sframe-and-properties.md` §8 defines them:

* **And** — `GNU_PROPERTY_X86_FEATURE_1_AND` and
  `GNU_PROPERTY_AARCH64_FEATURE_1_AND`: a bit survives only if every relocatable
  input sets it, and the property disappears entirely when some input carries no
  property of that type at all. Both architectures are checked, not only x86-64.
* **Or** — `GNU_PROPERTY_X86_ISA_1_NEEDED`: the union of the inputs' bits,
  kept whenever the result is non-zero.
* **AndOr** — `GNU_PROPERTY_X86_ISA_1_USED`: its *value* is the union of the
  inputs' bits, but it is *retained* only when every relocatable input declared
  that type. It is not a plain `OR` type: a union that no longer has unanimous
  backing is dropped, not kept.

 `-z x86-64-v2`/`-v3`/`-v4` **ORs the requested level
into `GNU_PROPERTY_X86_ISA_1_NEEDED`**; it is not an error for an input to
declare a higher level, and nothing here is checked as a link failure.

From this milestone, non-string `SHF_MERGE` sections (fixed-size elements —
`SHF_MERGE` without `SHF_STRINGS`, described by `sh_entsize`) are checked for
deduplication, and **the unit checked is the whole input section, not the
individual element**. Two input sections that map to the same output section
and whose `sh_entsize`, flags and byte content are identical MUST be emitted
once, and the symbols defined in them MUST come out at the same address.
Duplicate *elements* — a value repeated inside one section, or shared between
two sections that are not identical as a whole — are **not** folded, and the
checker asserts that they are not: it reads the merged section's `sh_size` back
and requires a symbol at the head of a section that survived separately not to
coincide with one in the deduplicated copy. Measured against this step's
reference: three 16-byte two-element inputs, two of them byte-identical, give a
**32-byte** output section in which one 8-byte element appears twice. An
element-granularity implementation gives 24 bytes with all three symbols
coincident, and is **not** what this milestone checks. All of this is subject
to the same `sh_addralign <= 1` cutoff string merging uses — an input demanding
stricter alignment is concatenated whole, not merged at all.
(`04-layout-and-sections.md` §3 closes by saying that what is *not* checked
there is tail merging, and that "from the milestone that introduces LoongArch64
and non-string section merging onward, the fixed-entry case is also checked on
its measured result" — this milestone, and this contract. Its string-merging
note is not a disclaimer for the fixed-entry case. `00-compatibility-profile.md` §10 is
not a disclaimer: it carries the measured 0.8.0–0.10.0 dedup table and agrees
with this contract. Where a specification document and this contract disagree
about what is checked, this contract is the authority.)

`--defsym SYM=EXPR` is new at this step — the option does not exist before
`0.8.0` — and is checked for: a decimal or `0x`-prefixed hex literal, a bare
reference to another symbol (copying its value and defining section index),
`SYMBOL+N` / `SYMBOL-N` with one offset, and a left-to-right chain of `+`/`-`
over those forms (`0x100-0x1`). Arithmetic is 64-bit and wraps. A literal or
an arithmetic result is an `SHN_ABS` symbol; a bare symbol reference is not —
it copies the target's defining section index, and the checker reads that index
back as well as the value. A symbol reference that no input defines MUST fail
the link with a non-zero status and MUST name the unresolved symbol on stderr —
`02-symbols-and-archives.md` §3's undefined-symbol rule applied to `--defsym` —
and, as everywhere else in this contract, only the status and the named token
are checked, never the rest of the wording. Operators
beyond `+`/`-` — `%`, the bitwise and shift operators, comparisons, the
ternary form, `MIN`/`MAX`, `ALIGN` — are **out of scope in `--defsym` at this
step**: measured, the step-7 Oracle instead treats the whole expression string
as a literal symbol name to look up and fails the link
(`error: Symbol '6*7' referenced by --defsym does not exist`). Do not read
"not here" as "next milestone" — they do not all arrive together. Measured at
`0.9.0` and `0.10.0`: `*`, `/`, `|`, `&`, `^`, `<<`, `>>`, `~`, `!`, the
comparisons, `&&`, `||`, `MIN` and `MAX` are correct from step 8; `%` and the
ternary `?:` parse at step 8 but are **silently mis-evaluated** there —
`--defsym x=100%7` defines 100 and `--defsym x=1?7:9` defines 1, each yielding
the left operand — and are correct only from step 9; and `ALIGN` is not usable
in `--defsym` at any milestone in this package — the one-argument form
evaluates to 0, because outside a script there is no location counter to
align, and the two-argument form fails to parse at 0.8.0, 0.9.0 and 0.10.0
alike. Script *symbol* assignment is checked here, but only in one
position. The **top-level** form — `NAME = expr;` written outside any
output-section description, either before the `SECTIONS` block or after it —
is new at this step and **is** checked: measured, `0.7.0` does not even parse it
as an assignment (it takes the leading token for an input filename and reports
``Failed to open input file `probe` ``), while `0.8.0` defines the symbol with
the correct value in both positions. It accepts the `+`/`-` expression forms
listed above for `--defsym`, and a constant-valued assignment yields an
`SHN_ABS` symbol. `s07-defsym/script-toplevel-assignment` covers it. One
measured property that case relies on and a linker must not be surprised by: a
symbol named only by a script expression is **not** a garbage-collection root
at this milestone, so under the default `--gc-sections` an expression like
`sum = gsym + 4 - 2;` whose `gsym` has no other reference evaluates to 0
instead of failing; only the `--no-gc-sections` or otherwise-referenced case is
checked. Every **other** assignment position is a later milestone and is not
checked here: assignment inside an output-section body to anything other than
the bare location counter `.` — a `+`/`-`/`|`-style expression, or `ALIGN(...)`
— is a hard parse error at `0.7.0` and `0.8.0` alike and first parses at step
8, and assignment *between* output-section descriptions, rather than inside
one, first parses at step 9. Assigning the bare location counter to a symbol
inside a body has parsed since step 4; the one case in this step that does it
is the inherited `gate-m04/script-custom-output-section` (`payload_lo = .;`).

### `.variant_pcs` / `.variant_cc` dynamic tags

`DT_AARCH64_VARIANT_PCS` (`DT_LOPROC + 5` = `0x70000005`, `d_un` unused) **is
checked from this milestone on**, and that supersedes the step-3 section above,
which records it as out of scope at *its* milestone. When a dynamically-linked
output imports a symbol carrying `STO_AARCH64_VARIANT_PCS` (the `.variant_pcs`
directive) and that symbol takes a PLT entry, the linker MUST emit the tag; an
otherwise identical link against an unmarked symbol MUST NOT carry it, and a
static link never carries it. `07-aarch64-relocations.md` §12.2 states the MUST
itself; the closing paragraph of that section, which declines to check "any of
the three", is written about the two PLT-property tags `DT_AARCH64_BTI_PLT` and
`DT_AARCH64_PAC_PLT` and is superseded here for `DT_AARCH64_VARIANT_PCS`. The
`STO_AARCH64_VARIANT_PCS` bit itself must still survive into `.symtab` and
`.dynsym`, exactly as it has since step 3.

`.variant_cc` on a RISC-V function symbol sets bit `0x80`
(`STO_RISCV_VARIANT_CC`) in the symbol's `st_other` — the same bit position
AArch64's `.variant_pcs` sets for `STO_AARCH64_VARIANT_PCS`
(`07-aarch64-relocations.md` §12.2), and the RISC-V psABI's analogue of it.
When a marked symbol is imported into a dynamically-linked output, the linker
MUST emit the dynamic tag `DT_RISCV_VARIANT_CC` (`0x70000001`, i.e.
`DT_LOPROC + 1`) with value 0. Measured against `wild-0.8.0`: the tag appears
on the executable that imports the marked symbol from a shared object, not on
the shared object that defines it.

`.init_array.NNNNN` and `.fini_array.NNNNN` must be ordered by priority, and the
checker checks the resulting pointer arrays' contents, not just their sizes. Both
arrays store **ascending** priority order — the linker does not reverse
`.fini_array`, and a runtime that wants destructors in reverse order walks the
array backwards itself. (Measured at 0.8.0, 0.9.0 and 0.10.0: two constructors at
priority 100 and 200 appear in that order in both arrays.) An unsuffixed
`.init_array`/`.fini_array` contribution (no `.NNNNN` priority) sorts **after**
every priority-suffixed one, regardless of its position on the command line.
The merged `.init_array`/`.fini_array` output section's `sh_addralign` MUST be
**at least 8**, even when every contributing input fragment declares
`sh_addralign` 1: these sections hold `Elf64_Addr`-sized pointers, and this is
a section-specific floor on top of the general maximum-of-contributions rule in
`04-layout-and-sections.md` §1.1.

Writing to an output path that already exists and is being executed must not
fail: either the in-place update succeeds or the linker unlinks and recreates.
Which of the two happens is **out of scope** — both are correct and the choice
leaves no trace in the output ELF — so no case distinguishes them.

### LoongArch64: what the backend is checked on

`s07-loongarch64` checks the relocation arithmetic
(`R_LARCH_PCALA_HI20`/`_LO12` with the `0x800` bias, `R_LARCH_B26` through the
PLT, `R_LARCH_64` in data), the GOT and the dynamic relocations that stand
behind it, the TLS models the toolchain on this image can emit — local-exec
(`R_LARCH_TLS_LE_HI20`/`_LO12`), general-dynamic, whose
`R_LARCH_TLS_DTPMOD64` / `R_LARCH_TLS_DTPREL64` pair must occupy **adjacent GOT
slots, module id first**, and local-dynamic, which is resolved exactly as
general-dynamic (`14-loongarch-relocations.md` §3.6) — and that the linked
executable runs correctly under `qemu-loongarch64-static`. TLSDESC is **not
checked**: `clang-16` on this image has no `-mtls-dialect=desc` for this target,
so no fixture can reach it.

`e_flags` is checked as an identity rule: every relocatable input must carry the
same value, the output carries that one value, and a link whose inputs disagree
must fail. Only the non-zero status is checked, never the message — the
reference's own wording for this changed between releases.

Two documented LoongArch areas are **out of scope at this step**, and no case
should be added for them here: `R_LARCH_CALL36`, which the image's `clang-16`
cannot be made to emit (`%call36` is rejected as an unknown operand modifier and
no LoongArch assembler is installed), and the relocation families
`14-loongarch-relocations.md` §3.7 marks "MUST be rejected", which the reference
aborts on rather than diagnosing — a case for those would be checking a crash,
so §3.7 states the rule without checking it.

### Negated glob classes in version scripts

`s07-version-glob-negation` checks the `[^...]` spelling of a negated glob
character class in a version script, on both the `global:` and the `local:` side
and in any position within the pattern. It MUST mean exactly what the POSIX
`[!...]` spelling means (`12-version-scripts.md`'s glob table): measured, the
caret was previously matched as an ordinary member of the set, so `foo[^ab]`
selected `fooa foob` where it must now select `fooc food`.
