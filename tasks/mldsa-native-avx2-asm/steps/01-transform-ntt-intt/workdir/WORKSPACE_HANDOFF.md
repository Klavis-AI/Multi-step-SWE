# Workspace handoff

This milestone runs in the same `/app` worktree as the last one, but may begin a
brand-new conversation with no memory of it. **The source tree in
`/app/mldsa-native` is your starting checkpoint** -- whatever the previous milestone left.
Do not reset it or undo work already there: earlier work is re-checked for
correctness here.

## Your target this milestone

The forward and inverse NTT.

The library routes each arithmetic operation through a backend hook. In
`mldsa-native/mldsa/src/native/x86_64/meta.h` the hooks are the `static MLD_INLINE
int mld_<op>_native(...)` definitions guarded by their `MLD_USE_NATIVE_*`
switches; the callers in `mldsa/src/poly*.c` and `mldsa/src/polyvec*.c` call
the hook and fall back to the portable C implementation when it reports
`MLD_NATIVE_FUNC_FALLBACK`. The portable C is complete, correct and slow, and
it stays in the tree.

## What is in the workspace

- `/app/mldsa-native` -- the source tree (complete library, its test suite, its
  benchmarks).
- `/app/BENCHMARK.md` -- how to build, how to check behaviour, how your work is
  timed, and which entry points this milestone covers.
- `/app/WORKSPACE_HANDOFF.md` -- this file.
- `/app/timer.sh` -- advisory countdown.

## Step log

Append what you changed and the speed difference you measured.
