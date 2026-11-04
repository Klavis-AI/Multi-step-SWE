#!/usr/bin/env bash
set -euo pipefail

# Oracle for this milestone: restore this filter family's upstream AviSynth+
# implementation. The patch beside this script is the whole input -- nothing is
# cloned, downloaded or fetched, and the source tree it applies to is the one
# this step's base carries.
solution_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
app_dir=${HARBOR_APP_DIR:-/app}

# Preflight. Without this a missing tool exits 127, the patch is never applied,
# the tree stays at base, correctness passes, and the step scores 1.0x --
# indistinguishable from a no-op. Fail loudly at the source instead.
for tool in patch cmake gcc g++ ninja; do
    command -v "$tool" > /dev/null         || { echo "ORACLE PREFLIGHT FAILED: '$tool' is not installed in this environment" >&2; exit 127; }
done

( cd "$solution_dir" && sha256sum -c SHA256SUMS )

cd "$app_dir/avisynthplus"
patch -p1 --forward --batch < "$solution_dir/oracle.patch"

# Prove the restored tree still builds.
cmake -S "$app_dir/avisynthplus" -B "$app_dir/build" -DCMAKE_BUILD_TYPE=Release -G Ninja > /dev/null
cmake --build "$app_dir/build" -j"$(nproc)" > /dev/null
echo "oracle applied and rebuilt: $(basename "$solution_dir")"
