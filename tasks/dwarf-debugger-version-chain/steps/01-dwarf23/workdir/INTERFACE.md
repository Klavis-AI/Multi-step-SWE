# INTERFACE.md — what this workspace builds, and how its output is compared

This file is the contract. It fixes the build command, the programs to produce,
the option sets they must accept, where they must be, and how their output is
compared. Every program below is a real tool whose behaviour is published, and
the expected output is the output those real tools produce.

## 1. The build

* The implementation language is **Java**, built by the JDK in this image
  (`javac`/`java`, release 25). The build must use only that JDK — no build tool
  downloads, no third-party jars, no native libraries beyond the platform C
  library, and no bundled or downloaded executables.
* **`make`**, run with the workspace root as the working directory and no
  arguments, must build everything.
* `make` must produce these executables, relative to the workspace root:

  | path | what it is |
  | --- | --- |
  | `bin/llvm-symbolizer` | LLVM's `llvm-symbolizer` command line and stdin protocol |
  | `bin/readelf` | GNU binutils `readelf`, the `--debug-dump=frames-interp` subset |
  | `bin/eu-stack` | elfutils' `eu-stack`, the `--core` subset |

  Each must be executable, runnable from any working directory, and a launcher
  for the JDK: each one must invoke `java`.

## 2. How your code is built and run

1. **Only text files smaller than 2 MiB** are copied out of the workspace — no
   `build/`, no `bin/`, no `tests-public/`, nothing binary — into a clean tree.
   A prebuilt jar, a vendored binary or a downloaded tool does not exist there;
   only the sources you wrote do. That tree is not at `/app`, and `/app` is not
   readable by your programs while they run from it, so the build
   and the launchers must find their files relative to the tree they are in,
   never through an absolute `/app` path.
2. **`make`** is run in that tree with the image's JDK. The build must succeed
   and must produce all three executables above.
3. Your executables are run over the ELF files supplied, and their standard
   output is compared to output recorded from the real tools.
4. Your programs must not invoke any of these as an external process:
   `llvm-symbolizer`, `llvm-symbolizer-19`, `llvm-addr2line`, `llvm-dwarfdump`,
   `llvm-cxxfilt`, `addr2line`, `readelf`, `eu-readelf`, `eu-addr2line`,
   `objdump`, `nm`, `size`, `strings`, `dwarfdump`, `eu-stack`, `gdb`,
   `gdbserver` or `c++filt`. Those programs are not present when your programs
   run from the clean tree; your programs must do the work themselves. A stub
   placed earlier on `PATH` than any of them records every call, so a call
   breaks this rule even when your program ignores the failure.

5. The implementation language is enforced. A file under the workspace whose
   name ends in `.c`, `.cc`,
   `.cpp`, `.cxx`, `.m`, `.rs`, `.go`, `.zig`, `.s`, `.S`, `.asm`, `.jar` or
   `.so`; a `.java` file that calls `System.load`, `System.loadLibrary` or
   `libraryLookup`, or that runs another program through Java's process API
   (`Runtime.exec` or `ProcessBuilder`) instead of doing its work in its own
   process; or a `Makefile`, `makefile`, `GNUmakefile`, `*.mk` or `*.sh`
   line whose command is `gcc`, `g++`, `cc`, `clang`, `clang++`, `rustc`,
   `cargo`, `zig`, `ghc`, `curl`, `wget`, `apt-get`, `aptitude`, `pip`, `pip3`,
   `mvn`, `gradle` or `sbt` — each of those is a breach. The rule is about the
   tool, not the spelling: a version- or release-suffixed name of one of those
   tools is that tool (so `gcc-14`, `clang++-19` and `pip3.11` are breaches
   exactly as `gcc`, `clang++` and `pip3` are), and a `make` recipe prefix (`@`, `-`,
   `+`) does not change what the command is. Compile scratch programs under
   `/tmp`, never inside the workspace.

ELF files are staged at **`/dw/out/fx/<name>`** and core files at
**`/dw/out/cores/<name>.core`** — the same absolute paths they were compiled at,
so paths recorded inside their debug information resolve unchanged.

## 3. The programs

### 3.1 `bin/llvm-symbolizer`

Reference: <https://llvm.org/docs/CommandGuide/llvm-symbolizer.html> (LLVM 19;
local copy: `/opt/docs/llvm-19/llvm-symbolizer.html`).

Options that must be supported:

```
--obj=<file>   --obj <file>   -e <file>   -e=<file>   --exe=<file>
                                                   the object file to read
--functions=<none|short|linkage>   -f=<value>      default: linkage
--output-style=<LLVM|GNU|JSON>                     default: LLVM
--no-inlines                                       report one frame per address
--basenames  -s         --relativenames            path style
--print-address  --addresses  -a                   print the address first
--pretty-print  -p                                 one line per frame; an
                                                   inlined caller continues on
                                                   its own ' (inlined by) ...' line
--adjust-vma=<offset>                              add offset to object addresses
```

An address that resolves to nothing prints `??` as the function and `??:0:0` as
the position (`??:0` in GNU style). `DATA <address>` prints the object's name,
then its start address and size as decimal numbers separated by a space, then
its declaration `file:line`; when nothing is known it prints `??`, `0 0` and
`??:?`.

Function names come from the debug information, with one override the
reference applies: under `--functions=linkage` (the default), when the ELF
symbol table answers for the address (the same lookup that answers for an
address with no debug information), the outermost frame (the only frame under
`--no-inlines`) takes that symbol's name, and in JSON output its
`StartAddress`. Inlined frames, the position and `StartLine` still come from
the debug information. A `.cold` part split off a function is therefore printed
under its own symbol's name, not under the name of the function the debug
information assigns it to.

Addresses are read from the command line, or one per line from standard input.
An input line may be an address on its own, an address prefixed with `CODE ` or
`DATA `, or `<file> <address>` naming the object explicitly.

### 3.2 `bin/readelf --debug-dump=frames-interp <file>`

Reference: `readelf(1)`, GNU binutils 2.44 (local copy:
`/opt/docs/binutils-2.44/readelf.html`). Prints the **interpreted** call-frame
tables for `.eh_frame` and for `.debug_frame`: one block per CIE and per FDE,
each FDE followed by the rows of its unwind table. Only the part of the output
from the first `Contents of the .eh_frame section:` or
`Contents of the .debug_frame section:` line onwards is compared, so the
`.eh_frame_hdr` dump is optional.

### 3.3 `bin/eu-stack --core=<core> -e <file> [-n N] [-s]`

Reference: `eu-stack(1)`, elfutils 0.192. Prints the stack of every thread found
in a core file: a `PID <pid> - core` line, a `TID <tid>:` line per thread, and one
`#<n>  0x<address> <function>` line per frame, innermost first. `-n N` caps the
number of frames per thread; `-s` adds an indented source position under each
frame.

## 4. How output is compared

The cases fall into four groups:

| group | what it compares |
| --- | --- |
| `sym-code` | `bin/llvm-symbolizer` on code addresses, across the option sets in §3.1 |
| `sym-data` | `bin/llvm-symbolizer` on `DATA` addresses |
| `cfi-interp` | `bin/readelf --debug-dump=frames-interp` |
| `core-stack` | `bin/eu-stack --core=… -e …` |

Comparison rules:

* `sym-code` and `sym-data` are compared **exactly**, after stripping trailing
  whitespace from each line. The unit of comparison is one address's output
  block (LLVM style and `DATA` blocks are separated by a blank line; with
  `--output-style=GNU --print-address` each block starts at the `0x…` line; with
  `--pretty-print` and with `--output-style=JSON` each line is a unit).
* `cfi-interp` and `core-stack` are compared after collapsing every run of spaces
  or tabs to one space, so column widths are not compared. The unit is one block
  for `cfi-interp` (blocks are separated by blank lines) and one line for
  `core-stack`.
* In every group, unit *i* of your output is compared with unit *i* of the
  expected output, and the group's unit count is the larger of the two — extra
  output counts as a mismatch exactly as missing output does.

### 4.1 Time limits

Three limits apply:

* **The build gets 900 seconds.** `make` is killed at that point, and that
  counts as a failed build.
* **Each case gets 120 seconds.** A program still running then is killed and
  that case fails; the other cases still run. Where a group states a tighter
  budget of its own, the tighter one decides that case.
* **The whole run gets 2400 seconds**, measured from its start, build included.
  When it expires the remaining cases are not run and every one of them counts
  as a **miss** — their expected units still count as unmatched. A program
  that answers correctly but slowly therefore loses the cases it never
  reached, so treat per-invocation start-up cost as part of the work: reading
  the debug information once per invocation rather than once per address is
  what keeps the whole set inside the budget.

## 5. Checking your work

`tests-public/` holds a representative sample of cases, with the real tools'
recorded output beside it:

```
bash tests-public/run-public-tests.sh
```

stages the sample at `/dw/out`, runs your three programs over it, and prints,
for each group, how many comparison units match under the rules in §4.
