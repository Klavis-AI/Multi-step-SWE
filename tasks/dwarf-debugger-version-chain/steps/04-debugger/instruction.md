# Step 4 — A debugger backend: ptrace control and the GDB Remote Serial Protocol

## 1. Implement this milestone

Everything this workspace already reads from an object stays as it is. What it
gains is the ability to **run** one: `make` must now also produce
**`bin/gdbserver`**, which launches a program under `ptrace(2)` and serves the
**GDB Remote Serial Protocol** on a TCP port so that a stock `gdb` can debug it.
`/app/INTERFACE.md` §3.4 fixes the command line and names the protocol
reference; your server must work when driven by `gdb -batch -nx -x <script>`
whose first command is `target remote 127.0.0.1:<port>`.

What such a session performs, and therefore what the server must
support:

* **Process control.** `fork(2)` a child that requests `PTRACE_TRACEME` and
  `execvp(3)`s the program, reap it with `waitpid(2)`, and report the stop to
  the client. Continue, single-step, and kill or detach on request.
  This is still a Java program and the language rule is unchanged: those calls
  are reached through the JDK's own foreign function and memory API,
  `java.lang.foreign`, binding libc with `Linker.nativeLinker()` and
  `SymbolLookup.defaultLookup()`, which loads no library. `/app/INTERFACE.md`
  §1 states what that permits and what it still forbids — `System.load`,
  `System.loadLibrary`, `SymbolLookup.libraryLookup` and any `.c`, `.so` or
  `.jar` are each forbidden.
* **The protocol.** Packet framing with `$…#<checksum>` and `+`/`-`
  acknowledgement; `qSupported` feature negotiation; `?` for the stop reason;
  `g`/`G` and `p`/`P` for registers; `m`/`M`/`X` for memory; `Z0`/`z0` for
  software breakpoints; `c`, `s` and `vCont`; `H` for thread selection;
  `qfThreadInfo`/`qsThreadInfo` and `qC` for threads; `qAttached`; `vKill`,
  `k` and `D`. Where the client needs to know the register layout, serve it —
  the protocol's own mechanism is `qXfer:features:read:target.xml`.
* **Stop replies** that tell the client why the inferior stopped, including the
  signal number, so that a breakpoint trap, a single-step completion and a
  fatal signal are distinguishable.
* **Registers and memory** for the x86-64 psABI (local copy:
  `/opt/docs/x86-64-psabi-1.0/x86-64-psABI-1.0.txt`) as gdb expects them, so that
  `info registers`, `x/…`, `print` over a local, `bt` and `finish` all agree
  with what the process actually contains.

and the unwinder gains the cores a running process produces:

* **Multi-threaded cores.** Every `NT_PRSTATUS` note is a thread with its own
  register set; `bin/eu-stack` reports each of them, in note order.
* **Cores of signal-stopped processes**, taken as the signal is delivered,
  before any handler runs.

## 2. Read these local references

**Normative semantics:** the GDB Remote Serial Protocol as published in the GDB
manual (Appendix E; local copy of the GDB 16.3 edition:
`/opt/docs/gdb-16.3/remote-protocol.txt`, with Appendix G, target descriptions,
in `target-descriptions.txt` beside it), `ptrace(2)` and `waitpid(2)` as
published in the Linux man pages (local copies:
`/opt/docs/man-pages-6.9.1/ptrace.2.txt` and `wait.2.txt`, with `fork.2`,
`execve.2`, `exec.3`, `personality.2`, `core.5` and `elf.5` beside them), and
the ELF core-file layout in the System V gABI (local copy:
`/opt/docs/gabi/contents.html`). The DWARF texts in
`/app/spec/` remain the definition of the debug information the rest of the
workspace reads.

**Additional files:** `/app/INTERFACE.md` — the build command, the executables,
their published option sets, where the ELF and core files are staged, the groups
of cases, and the exact comparison rules. `/app/tests-public/` —
a sample with the real tools' recorded output beside it, including recorded gdb
sessions; `bash /app/tests-public/run-public-tests.sh` runs your programs on it
and compares their output by those same rules. `/app/WORKSPACE_HANDOFF.md` —
keep it accurate.

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
