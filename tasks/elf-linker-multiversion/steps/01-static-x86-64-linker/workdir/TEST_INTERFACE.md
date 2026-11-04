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

**The gate** is mandatory and adds no new requirement. There is no earlier
milestone to regress at this step, so there are no `gate-m*` groups and the
gate here is proof of life only: your workspace builds with `cargo build
--release --bin wild`, and the `wild` it builds then starts when invoked.
"Starts" means the process launches and returns an ordinary exit status -- any
status, including a non-zero one; only a timeout, a failure to execute, or
death by a signal counts as not starting. The probe runs `wild --version` and
then a bare `wild`. It does **not** have to link anything successfully to
clear the gate — linking is what the requirement groups measure. If the gate
fails, the step is not satisfied, whatever else passed, and the verifier's
`reward_details.json` names the component that failed and why.

**Requirement groups** are this milestone's own new capability, plus its
peripheral breadth. They are **not** uniform: each group covering this
milestone's headline capability and each peripheral one is listed in this
step's section below, which is what to read before you decide what to build
first.

The consequence worth internalising: clearing the gate is not a requirement
in its own right. The scaffold you were handed already clears it and satisfies
none of the new work.

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
