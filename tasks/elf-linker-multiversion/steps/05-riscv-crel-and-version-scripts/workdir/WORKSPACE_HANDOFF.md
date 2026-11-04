# Workspace handoff — step 05 (RISC-V, CREL and the version-script grammar)

This task is cumulative. `/app` is carried from step to step: whatever you leave
behind is exactly what the next step's agent starts from, and it is also what
this step's grader builds. Nothing else travels — not your shell history, not
anything you wrote outside `/app`, not your build directory.

## What is preserved, and what is not

| path | survives into the next step |
| --- | --- |
| everything under `/app` | yes |
| `/app/target/` | **no** — excluded from the artifact; the grader rebuilds from source |
| `/app/.git/` | **no** — excluded from the artifact |
| anything outside `/app` | **no** |

Two consequences worth stating plainly. First, a workspace that only builds
because of something cached in `target/` does not build at all when it is
graded. Second, you may use Git inside `/app` freely while you work, but its
history will not be there next time: put anything you want to keep in tracked
files, not in commit messages.

## Preservation rules

- `TEST_INTERFACE.md`, `WORKSPACE_HANDOFF.md` and everything under `spec/` are
  the shipped contract. Leave them where they are. Later steps replace them with
  their own, extended, editions; deleting or rewriting them loses the record of
  what you were asked to build.
- `timer.sh` is the step clock. Leave it in place.
- Keep the workspace buildable when you stop. A step that ends mid-refactor with
  a workspace that does not compile scores zero *and* hands the next step a
  broken tree.

## Checkpointing while you work

```sh
cd /app
cargo build --release --bin wild        # the exact build the grader runs
ls -l "${CARGO_TARGET_DIR:-target}/release/wild"
```

A quick end-to-end smoke test that needs nothing but the toolchain already in
the image:

```sh
cd /tmp && rm -rf smoke && mkdir smoke && cd smoke
cat > s.s <<'ASM'
        .text
        .globl _start
_start: mov     $1, %eax
        mov     $1, %edi
        lea     msg(%rip), %rsi
        mov     $3, %edx
        syscall
        mov     $60, %eax
        mov     $7, %edi
        syscall
        .section .rodata
msg:    .ascii  "ok\n"
ASM
clang-16 -c s.s -o s.o
"${CARGO_TARGET_DIR:-/app/target}/release/wild" -o smoke.out s.o && ./smoke.out; echo "exit=$?"
```

Compare anything you are unsure about against the system linkers, which are
installed for exactly this purpose:

```sh
ld      -o ref-ld.out   s.o
ld.lld-16 -o ref-lld.out s.o
llvm-readelf-16 --wide -h -l -S ref-ld.out
```

They are reference material. Do not invoke them from inside `wild`: the grader
scores the ELF *your* linker produces, and a linker that shells out to another
linker scores zero.

## Picking this up from a cold start

1. `cat /app/spec/README.md` — the specification set for this milestone.
2. `cat /app/TEST_INTERFACE.md` — the contract, including the explicit list of
   what is not scored.
3. `cargo build --release --bin wild` — confirm the tree you inherited builds
   before you change anything.
4. Run the smoke test above. It exercises the shortest path through the linker
   and tells you immediately whether the inherited tree works at all.
