# How to check your work

## Build

```bash
cd /app/mlkem-native
make lib OPT=1 AUTO=1 -j4 BUILD_DIR=/app/build
```

`OPT=1` is the default and is what keeps the native arithmetic backend
compiled in. `AUTO=1` **is not a default — pass it explicitly, as every command
below does**: it is what enables the host's instruction set for the C compiler
(`-mavx2 -mbmi2` on this machine), and without it `MLK_SYS_X86_64_AVX2` is never
defined, `mlkem/src/native/meta.h` never includes the x86_64 backend, and this
stage's dispatch hook does not exist at all — so your kernel would be dead code
and you would measure ~1.0x with no indication why.

## Behaviour must not change

The library has to produce exactly the bytes it produced before. All of these
are re-run, with hidden inputs, when your work is checked.

1. **The project's own functional and known-answer suites** must gain no new
   failure:

   ```bash
   cd /app/mlkem-native
   make run_func run_kat OPT=1 AUTO=1 -j4 BUILD_DIR=/app/build
   ```

   These cover all three parameter sets (ML-KEM-512/768/1024). **The tree you
   inherit passes all six of these targets** — the gate is baseline-relative, so
   only *new* failures are in scope, and there are none to start from.

2. **End-to-end key vectors and ciphertexts.** For each parameter set, key
   generation from fixed coins, encapsulation, decapsulation and the
   re-encapsulation check must produce byte-identical output to the tree you
   started from. FIPS 203 defines every output exactly, so this is equality,
   not a tolerance.

3. **The functions in this stage** must produce byte-identical output to the
   versions you inherited, checked directly rather than through the KEM. Every
   function this stage covers is compared this way — the raw output of the entry
   point you implement against the raw output of the one you inherited, on the
   same inputs.
   

The checker also rebuilds your tree with this milestone's dispatch hooks restored
to the fallback, and fails the milestone if the tree as delivered is more than
25% slower end-to-end than that. Its purpose is to catch a kernel that wins the
microbenchmark while hurting the real pipeline.

## How speed is measured

**Cycles, per function, on the entry point the library installed.** The checker
compiles a harness against your tree and against this step's base build, calls
the same public entry point in both on identical inputs, and divides:
`speedup_f = cycles_base(f) / cycles_yours(f)`.

This step covers `mlk_poly_compress_dv`, in all three parameter sets — 3 measurements in total.

Each entry point is measured on its own ratio, so an entry point you leave
alone contributes exactly 1.0x: there is no benefit to making one of them fast
and neglecting another. A regression is floored at 1.0x rather than measured
below it.

**Measure it yourself with the project's own component benchmark**, which times
the poly-level entry points and prints median cycles:

```bash
cd /app/mlkem-native
make bench_components CYCLES=PMU OPT=1 AUTO=1 -j4 BUILD_DIR=/app/bench
/app/bench/mlkem768/bin/bench_components_mlkem768
/app/bench/mlkem512/bin/bench_components_mlkem512
/app/bench/mlkem1024/bin/bench_components_mlkem1024
```

Run it on an idle machine — the box is shared and a contended host inflates
every number. The lines that matter for this milestone are `mlk_poly_compress_dv`.

Comparing your tree against a stashed copy of the tree you started from is the
honest way to read it; the checker's denominator is measured in its own
container, so absolute cycle counts are not comparable between the two.

Note what the benchmark's own `BENCH_NATIVE_OK` entries do: they assert the
native path was actually taken rather than silently timing the C fallback. A
hook that still returns `MLK_NATIVE_FUNC_FALLBACK` makes the benchmark drop
output and still exit 0 — but **not the line this stage is measured on**. The one
signal is on **stderr**: a `CHECK` that fires prints
`ERROR (<file>,<line>)` before returning, so read stderr, not just stdout.
`MLK_TEST_FAIL()` expands to `return 1` in this binary, so a failing `CHECK`
returns from `bench()`, whose result `main` ignores: everything *after* that
check vanishes from the output and the process still exits 0. The checks run in a
fixed order, so the *first* failure truncates all the rest.

What that does and does not hide. The `*_native` breadcrumb lines are behind
`BENCH_NATIVE_OK`, so a declining hook drops them (and a toggle you never defined
means they were never compiled in at all). **The plain `BENCH` line for this
stage's entry point is printed before any of those checks, so it always appears —
a number on it is not evidence that your hook ran.** To confirm your kernel is
live, look for its `*_native` line, not for the plain one.

## `make unit` — out of scope, and it compiles as you inherit the tree

The unit tests gate their helper generators on the same `MLK_USE_NATIVE_*`
toggles you are switching on, so a toggle missing from those guards would make
`test/src/test_unit.c` fail to compile with an implicit-declaration error. **In
the tree you inherit it does not**: those guards are `||`-lists that already
contain the earlier milestones' toggles, which this point in the ladder always
defines, so they are satisfied whatever you add. Verified by building with this
stage's toggles added — `make unit` compiles and passes. It is out of scope;
this note exists so that you do not treat a failure here as a kernel bug.

## What success means here

- `mlk_poly_compress_dv` measurably faster in all three parameter sets;
- no new failure in `run_func` / `run_kat`;
- end-to-end key and ciphertext vectors unchanged;
- the pipeline not slower than your own tree with this stage's hooks
  neutralised.
- **constant-time output**: no branch or memory index on a secret value.
  Your instruction states this as a requirement. Nothing in the checker
  detects a violation, so treat it as one — it is part of the acceptance
  criteria whether or not a check can see it.
