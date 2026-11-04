# Step 1 — A DWARF 2/3 symbolizer, CFI table reader and core unwinder

## 1. Implement this milestone

Build, in this workspace and from nothing, a Java program that reads the debug
information in an ELF object and answers the three questions three real tools
answer. `/app/INTERFACE.md` fixes the build command, the three executables
`make` must produce, the options they accept and how their output is compared; the ELF
files it names are staged at `/dw/out`.

The debug information is **DWARF version 2 (1993) and version 3 (2005)**, as
published by the DWARF Debugging Information Format Committee; the normative
text is in `/app/spec/`. Your programs must work out what they need from the
object itself — the DWARF version of each compilation unit comes from that
unit's header, never from a command-line option.

* **`bin/llvm-symbolizer`** — for a code address: the function containing it and
  the source position of that address, resolved through `.debug_info`,
  `.debug_abbrev`, `.debug_str` and the `.debug_line` number program (DWARF 2
  §6.2, DWARF 3 §6.2), including the compilation-unit and subprogram address
  ranges (`DW_AT_low_pc`/`DW_AT_high_pc` and `DW_AT_ranges` into
  `.debug_ranges`). For a data address: the object's name, start and size. When
  an address has no debug information the ELF symbol table answers instead. With
  the default function-name kind (`linkage`) the symbol table also names the
  outermost function where debug information exists, as llvm-symbolizer does:
  when an ELF symbol answers for the address, its name replaces the one the
  debug information gives, so a `.cold` part split off a function is printed
  under its own symbol's name (§3.1 of `INTERFACE.md` has the details). When
  nothing answers the tool says so in the form §3.1 of `INTERFACE.md`
  documents. The option set in that section — output styles, function-name
  kinds, path styles, address printing, VMA adjustment — is part of the
  milestone, as is the stdin command protocol.
* **`bin/readelf --debug-dump=frames-interp`** — the **interpreted** call-frame
  table for `.eh_frame` and for `.debug_frame`: each CIE and FDE, and for each
  FDE the rows its call-frame program produces, one row per address at which a
  rule changes. This means running the DWARF call-frame instruction set (DWARF 3
  §6.4.2) over the CIE's initial instructions and then the FDE's — the required
  tables exercise `DW_CFA_advance_loc*`, `DW_CFA_def_cfa*`, `DW_CFA_offset*`,
  `DW_CFA_undefined` and expression rules — with the augmentation and pointer
  encodings `.eh_frame` uses, and with `.debug_frame`, whose CIEs differ.
* **`bin/eu-stack --core=<core> -e <exe>`** — the stack of every thread in an ELF
  core file: read the `NT_PRSTATUS` notes for the register sets and the `PT_LOAD`
  segments for memory, then walk the frames by applying the call-frame table at
  each program counter, and name each frame the way the symbolizer does with
  `--no-inlines` (one frame per address, no inlined callers).

Both `x86-64` and 32-bit-offset DWARF appear in the objects to be read; addresses are
unsigned 64-bit quantities and the objects are little-endian.

## 2. Read these local references

**Normative semantics:** `/app/spec/dwarf-2.0.0.txt` (DWARF 2.0.0) and
`/app/spec/Dwarf3.txt` (DWARF 3) — the definition of the format to read. The ELF
container is the System V gABI (local copy: `/opt/docs/gabi/contents.html`)
and the x86-64 psABI (local copy:
`/opt/docs/x86-64-psabi-1.0/x86-64-psABI-1.0.pdf`, text in
`x86-64-psABI-1.0.txt` beside it).

**Additional files:** `/app/INTERFACE.md` — the build command, the executables,
their published option sets with the documents that define them, where the ELF
files are staged, the groups of cases, and the exact comparison rules.
`/app/tests-public/` — a sample of ELF files and core files with the real
tools' recorded output beside them; `bash /app/tests-public/run-public-tests.sh`
runs your programs on it and compares their output by those same rules.
`/app/WORKSPACE_HANDOFF.md` — leave a reproducible handoff there.

## 3. Meta requirements

- The implementation budget is 7,200 seconds. Check `bash /app/timer.sh`;
  the configured timeout is authoritative and resets for this step.
- Work in the cumulative `/app` workspace and preserve the supplied sources,
  documents and handoff files. Do not create a prose answer or an alternate
  entry point unless section 1 requires one.
- Your programs must build with `make` from a copy of `/app` that holds only
  its text files under 2 MiB, without `build/`, `bin/` and `tests-public/`, so
  leave it source-only. Compile scratch programs under `/tmp`, not in the
  workspace, and keep the files you write in plain ASCII.
- Do not cheat by using online solutions or hints
  specific to this task. Work only from this workspace, its documentation and
  `/opt/docs`; do not search the system for other implementations or answers.
- Run the local checks named above and keep `/app/WORKSPACE_HANDOFF.md` an
  accurate description of the repository.
