# Step 3 — DWARF 5: new encodings, split units, and a new file-numbering base

## 1. Implement this milestone

The objects this workspace reads now include compilation units emitted at
**DWARF version 5 (2017)**, normatively `/app/spec/DWARF5.txt`. The version comes
from each unit's header, never from a command-line option, and objects built at
DWARF 2, 3 and 4 must keep resolving exactly as they do today — the milestone
covers both. `/app/INTERFACE.md` fixes the build command, the three
executables, their option sets and how each group's output is compared.

DWARF 5 changes information you already read:

* **File numbering changes base** (§6.2.4). A version 5 line-number program
  numbers file entries from **0**, and entry 0 is the primary source file; at
  versions 2 through 4 the entries are numbered from 1. An implementation that
  keeps the old base reports every file name shifted by one.
* **The unit header changes shape** (§7.5.1). A version 5 unit carries a
  `unit_type` byte and moves `address_size` before `debug_abbrev_offset`, and the
  type of unit decides what follows. A ≤4 header parser misreads every version 5
  unit.
* **The line-number program header is rebuilt** (§6.2.4): `directory_entry_format`
  and `file_name_entry_format` describe the entries with `DW_LNCT_*` content
  codes and forms, and the entries may carry an MD5 hash.

and it adds encodings and sections:

* **Indirect strings and addresses.** `DW_FORM_strx`, `strx1`-`strx4` index
  `.debug_str_offsets` from the unit's `DW_AT_str_offsets_base`; `DW_FORM_addrx`,
  `addrx1`-`addrx4` index `.debug_addr` from `DW_AT_addr_base` (§7.26, §7.27).
* **`.debug_rnglists` and `.debug_loclists`** (§7.25, §7.29) with the `DW_RLE_*`
  and `DW_LLE_*` entry kinds, base-address selection, and the `DW_FORM_rnglistx`
  / `DW_FORM_loclistx` index forms.
* **Split DWARF** (§7.3.2). A skeleton unit in the executable names a `.dwo` file
  through `DW_AT_dwo_name`, relative to `DW_AT_comp_dir`; the debugging
  information proper lives there, while `.debug_addr` and the line-number program
  stay with the skeleton. **Symbolization** of such an object must resolve to the
  same answers as if the information had not been split; the `FRAME` command and
  the section dumps read the object's own information, as the tools do.
* **Compressed debug sections.** A section may carry `SHF_COMPRESSED` with an
  `Elf64_Chdr` header and zlib-deflated contents (ELF gABI); symbolization of
  such an object must be unaffected.
* **`.debug_names`** (§6.1.1), the name index, dumped by
  `bin/readelf --debug-dump=gdb_index`: its header and augmentation, the CU, TU
  and foreign-TU tables, the bucket statistics, and one line per symbol with its
  hash, tag and `DW_IDX_*` attributes.

`bin/readelf --debug-dump=loc` must print both `.debug_loc` and the new
`.debug_loclists`; `/app/INTERFACE.md` §3.2 names what each keyword prints.

## 2. Read these local references

**Normative semantics:** `/app/spec/DWARF5.txt` is the version added here;
`/app/spec/DWARF4.txt`, `/app/spec/Dwarf3.txt` and `/app/spec/dwarf-2.0.0.txt`
remain the definition of the older units, which must still be handled. The container
format is the System V gABI (for `SHF_COMPRESSED`; local copy:
`/opt/docs/gabi/ch4.sheader.html`, contents at `/opt/docs/gabi/contents.html`)
and the x86-64 psABI (local copy:
`/opt/docs/x86-64-psabi-1.0/x86-64-psABI-1.0.pdf`, text in
`x86-64-psABI-1.0.txt` beside it).

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
  documents and handoff files. Do not create a prose answer or an alternate entry
  point unless section 1 requires one.
- Your programs must build with `make` from a copy of `/app` that holds only
  its text files under 2 MiB, without `build/`, `bin/` and `tests-public/`, so
  leave it source-only. Compile scratch programs under `/tmp`, not in the
  workspace, and keep the files you write in plain ASCII.
- Do not cheat by using online solutions or hints
  specific to this task. Work only from this workspace, its documentation and
  `/opt/docs`; do not search the system for other implementations or answers.
- Run the local checks named above and keep `/app/WORKSPACE_HANDOFF.md` an
  accurate description of the repository.
