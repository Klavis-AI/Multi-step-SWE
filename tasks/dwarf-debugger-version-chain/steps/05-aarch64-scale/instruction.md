# Step 5 — aarch64, mixed-version objects, 64-bit DWARF, and bulk symbolization

## 1. Implement this milestone

The objects this workspace reads are no longer only x86-64, no longer all one
DWARF version, and no longer all in the 32-bit DWARF format. Everything already
supported must keep working; `/app/INTERFACE.md` fixes the build command, the
executables, their option sets and how each group's output is compared.

The build still produces all four executables from step 4 — `bin/llvm-symbolizer`,
`bin/readelf`, `bin/eu-stack`, and `bin/gdbserver` — and each one must contain
the bytes `java`, so all four must remain JDK launchers.
`bin/gdbserver` gains no new behaviour at this milestone, but the
launcher rule applies to it exactly as to the others: if `bin/gdbserver` is
executable, its first 8 KiB must contain the bytes `java`.

* **aarch64.** Objects with `EM_AARCH64` in their ELF header, built for the
  AArch64 procedure call standard. Symbolization must work on them exactly as it
  does on x86-64, and the call-frame tables must be produced with aarch64's
  DWARF register numbering (x0-x30, sp, and the return address in the column the
  CIE names, which is 30 rather than 16). Some of these objects are compiled
  with `-mbranch-protection=pac-ret`: their call-frame programs carry
  `DW_CFA_AARCH64_negate_ra_state` (vendor opcode 0x2d, which is
  `DW_CFA_GNU_window_save` on other architectures — the machine decides which),
  toggling whether the return address in that frame is signed with pointer
  authentication.
* **Mixed-version objects.** Executables whose compilation units were emitted
  at *different* DWARF versions — units at versions 2, 3, 4 and 5 side by side,
  each with its own header shape, its own form encodings
  and its own line-number program. Every unit must resolve by the rules of its
  own version; an address in one must not be answered by the conventions of
  another.
* **64-bit DWARF** (DWARF 5 §7.4). A unit whose initial length field is the
  escape value `0xffffffff` followed by a 64-bit length uses 8-byte section
  offsets throughout — in unit headers, in `DW_FORM_strp`, `DW_FORM_sec_offset`
  and every other offset-valued form, and in the line-number program header.
  The same source compiled with and without this format must symbolize
  identically.
* **Bulk symbolization.** One workload feeds 578 addresses to
  `bin/llvm-symbolizer` in a single invocation and requires the answers to be
  correct **and** the invocation to finish inside the budget
  `/app/INTERFACE.md` §4 states. Reading the debug information once per
  invocation rather than once per address is the difference between finishing
  in time and timing out.

## 2. Read these local references

**Normative semantics:** `/app/spec/DWARF5.txt` (including §7.4 for the 64-bit
format), and `/app/spec/DWARF4.txt`, `/app/spec/Dwarf3.txt`,
`/app/spec/dwarf-2.0.0.txt` for the older units that still appear. The
architecture references are the ELF gABI (local copy:
`/opt/docs/gabi/contents.html`) and the AArch64 psABI, which defines the DWARF
register numbering and the `DW_CFA_AARCH64_*` vendor opcodes (local copies,
release 2024Q3, each PDF with a `.txt` beside it:
`/opt/docs/aarch64-abi-2024Q3/aadwarf64.pdf`, DWARF for the Arm 64-bit
Architecture; `aaelf64.pdf`, ELF for the Arm 64-bit Architecture; and
`aapcs64.pdf`, the procedure call standard).

**Additional files:** `/app/INTERFACE.md` — the build command, the executables,
their published option sets, where the ELF and core files are staged, the groups
of cases, the throughput budget, and the exact comparison rules.
`/app/tests-public/` — a sample with the real tools' recorded output beside it;
`bash /app/tests-public/run-public-tests.sh` runs your programs on it and
compares their output by those same rules. `/app/WORKSPACE_HANDOFF.md` — keep it
accurate.

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
