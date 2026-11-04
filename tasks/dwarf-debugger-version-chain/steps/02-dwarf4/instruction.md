# Step 2 — DWARF 4: type units, location lists, the expression machine, inlined frames

## 1. Implement this milestone

The objects this workspace reads now include compilation units emitted at
**DWARF version 4 (2010)**, normatively `/app/spec/DWARF4.txt`. The version is
read from each unit's header, never from a command-line option, and objects
built at DWARF 2 and 3 must keep resolving exactly as they do today — the
milestone covers both. `/app/INTERFACE.md` fixes the build command, the
three executables, their option sets and how each group's output is compared.

DWARF 4 changes the meaning of information you already read:

* **`DW_AT_high_pc` changes class** (§2.17.2). At DWARF 2 and 3 it is an address;
  at DWARF 4 a form of class `constant` makes it an **offset from `DW_AT_low_pc`**.
  A unit that gets this wrong reports the wrong function for most addresses.
* **The line number program header gains a field** (§6.2.4): version 4 adds
  `maximum_operations_per_instruction` after `minimum_instruction_length`, and
  the `op_index` register it introduces takes part in every address advance.
* **Type units** (§3.1.4) put type DIEs in their own units with a 64-bit
  signature, in `.debug_types` sections, referenced by `DW_FORM_ref_sig8`. A
  `DW_AT_type` that crosses into a type unit must still resolve.

and it adds work that was not required before:

* **Location lists** (§2.6.2) in `.debug_loc`, and the **DWARF expression stack
  machine** (§2.5) whose operations appear both there and in call-frame rules.
  Both are defined at DWARF 2; what is new here is that they are required.

* **Inlined frames.** `bin/llvm-symbolizer`'s default is to report the whole
  inline chain for an address, innermost first: each `DW_TAG_inlined_subroutine`
  containing the address contributes a frame whose source position is the one
  the line table gives, while the *caller's* position comes from that DIE's
  `DW_AT_call_file`, `DW_AT_call_line` and `DW_AT_call_column`. The outermost
  frame is the containing `DW_TAG_subprogram`. `--no-inlines` keeps reporting a
  single frame.
* **Optimized-code locations.** The objects include `-O1` and `-O2`
  builds, where code is inlined, reordered and split into cold parts.
* **The `FRAME` command**, which reports a function's formal parameters and
  local variables with their declaration positions, frame offsets and type
  sizes — so `DW_AT_frame_base`, `DW_OP_fbreg` and the type graph
  (`DW_AT_byte_size` through typedefs, qualifiers, pointers and arrays, and
  through type-unit references) all have to work.
* **C++ objects**, whose `DW_AT_linkage_name` is an Itanium C++ ABI mangled
  name. `--functions=linkage` (the default) reports the demangled signature,
  `--no-demangle` the raw symbol, and `--functions=short` the `DW_AT_name`.
  The mangling grammar is published at
  <https://itanium-cxx-abi.github.io/cxx-abi/abi.html#mangling> (local copy:
  `/opt/docs/itanium-cxx-abi/abi.html#mangling`).
* **Two more `readelf` dumps**: `--debug-dump=frames`, the raw call-frame
  instruction listing including DWARF expressions, and `--debug-dump=loc`, the
  `.debug_loc` location lists. `/app/INTERFACE.md` §3.2 names what each prints.

## 2. Read these local references

**Normative semantics:** `/app/spec/DWARF4.txt` (DWARF 4) is the version added
here; `/app/spec/dwarf-2.0.0.txt` and `/app/spec/Dwarf3.txt` remain the
definition of the older units, which must still be handled.

**Additional files:** `/app/INTERFACE.md` — the build command, the executables,
their published option sets, where the ELF files are staged, the groups of
cases, and the exact comparison rules. `/app/tests-public/` — a
sample of ELF and core files with the real tools' recorded output beside them;
`bash /app/tests-public/run-public-tests.sh` runs your programs on it and
compares their output by those same rules. `/app/WORKSPACE_HANDOFF.md` — keep it
an accurate description of the repository.

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
