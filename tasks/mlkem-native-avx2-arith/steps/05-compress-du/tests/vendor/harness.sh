#!/usr/bin/env bash
# harness.sh <tree> <builddir> <scheme> <out>  -- compile bench_step.c against a tree
set -euo pipefail
TREE="$1"; BLD="$2"; SCHEME="$3"; OUT="$4"
gcc -O3 -std=c99 -pthread \
  -I "$TREE/mlkem" -I "$TREE" \
  -DMLK_CONFIG_PARAMETER_SET="$SCHEME" \
  -DMLK_FORCE_X86_64 \
  -DMLK_CONFIG_USE_NATIVE_BACKEND_ARITH \
  -DMLK_CONFIG_USE_NATIVE_BACKEND_FIPS202 \
  -DNDEBUG -mavx2 -mbmi2 \
  -o "$OUT" /tests/vendor/bench_step.c "$BLD/libmlkem$SCHEME.a"
