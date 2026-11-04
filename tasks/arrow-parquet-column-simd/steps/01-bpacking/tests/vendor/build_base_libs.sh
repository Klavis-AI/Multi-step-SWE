#!/usr/bin/env bash
# Runs once, at verifier-image build time, before any per-step file is copied
# into the image.  Everything it produces is a property of the ladder's base
# (level L0), not of any one step, which is why all six verifier images can
# share this layer.
#
# This half builds the base TREE only -- it deliberately does not read the
# measurement harnesses, so that editing a harness invalidates only the cheap
# layer in build_base_probes.sh and not the ~50-minute build below.
#
# It leaves behind:
#   /src                       the L0 source tree
#   /bld                       its build (reused, at the same paths, for the
#                              candidate at measurement time so ccache stays hot)
#   /td/arrow-testing          upstream test data
#   /td/parquet-testing        upstream parquet test data
#   /baseline/                 the base's own test failures and its cpu facts
set -euo pipefail
. /tests/vendor/PIN

echo "== clone arrow at $ARROW_SHA =="
git clone --quiet "$ARROW_REPO" /src
cd /src
git checkout --quiet "$ARROW_SHA"
test "$(git rev-parse HEAD)" = "$ARROW_SHA"
rm -rf .git

# A pristine copy of the same tree, kept only for the similarity diagnostic.  It is taken BEFORE stripping, so it is upstream's own code.
cp -a /src /upstream

echo "== strip the base =="
python3 /tests/vendor/strip.py /src $(cat /tests/vendor/base_stages.txt)

echo "== build the base =="
export BUILD_JOBS=${BUILD_JOBS:-4}
export BUILD_TARGETS="arrow_static arrow_compute_static parquet_static arrow_acero_static $(tr '\n' ' ' < /tests/vendor/test_targets.txt)"
bash /tests/vendor/build_arrow.sh /src /bld

