#!/usr/bin/env bash
# Runs once, at the start of step 13, before the agent phase -- the harness
# runs `<workdir>/setup.sh` after uploading this step's workdir. It is
# environment, not task: it holds the container's cargo to this milestone's
# dependency set.
#
# One image serves all fifteen steps, so its registry cache is the union of
# every milestone's crates. The grader's image carries this milestone's set
# alone, and a dependency outside it would otherwise resolve here and fail
# there -- silently, which is the one failure mode a candidate cannot see
# coming. `/opt/vendor/13` is that set, as a cargo directory source
# (environment/bin/make-vendor.py builds it in both images from the same
# locks); pointing `crates-io` at it makes an unlisted dependency fail here,
# in the grader's own words:
#
#     no matching package named `NAME` found
#     location searched: directory source `/opt/vendor/13`
#
# Idempotent, and re-run at every step, so it always names this step's set.
set -eu

vendor=/opt/vendor/13
config="${CARGO_HOME:-/usr/local/cargo}/config.toml"

if [ ! -d "$vendor" ]; then
    echo "setup: $vendor is missing from the image -- the milestone's" >&2
    echo "dependency set cannot be pinned, so a build here would not" >&2
    echo "predict the grader's. Failing loudly rather than scoring blind." >&2
    exit 1
fi

mkdir -p "$(dirname "$config")"
cat > "$config" <<EOF
# Written by /app/setup.sh at the start of step 13. Do not edit: this is
# the milestone's dependency set, and the grader resolves against the same one.
[source.crates-io]
replace-with = "milestone"

[source.milestone]
directory = "$vendor"
EOF
