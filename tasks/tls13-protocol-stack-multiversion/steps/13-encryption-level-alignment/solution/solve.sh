#!/usr/bin/env bash
set -euo pipefail

# Oracle for step 13: apply the exact upstream release-to-release
# transition rustls 0.23.31 -> 0.23.45 over the cumulative /app
# tree left by step 12. Nothing is cloned, downloaded or fetched,
# and no cargo command is run.
#
#   upstream repository  https://github.com/rustls/rustls.git
#   upstream release     v/0.23.45 (commit 2976d90fd1c2db6b518700dd101b714069cfcb17)
#   previous checkpoint  v/0.23.31 (commit 647ece13463bbb6eaf756146596fe182644424b1)
#   licence              Apache-2.0 OR ISC OR MIT (see LICENSE beside this file)
#   excluded             test-ca bogo/bogo target -- verifier-owned or build output
#
# The Cargo.lock transition is a hunk inside this patch, not a side file:
# both sides of the diff were staged with the frozen lockfile in place, so
# applying the patch leaves the lock the `--locked` build needs. Its digest is
# asserted below all the same.

solution_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
app_dir=${HARBOR_APP_DIR:-/app}

if ! ( cd "$solution_dir" && sha256sum -c --quiet --strict SHA256SUMS ); then
    printf "solve: payload checksum mismatch under %s; %s not touched\n" \
        "$solution_dir" "$app_dir" >&2
    exit 1
fi

[ -d "$app_dir" ] || { printf "solve: no %s\n" "$app_dir" >&2; exit 1; }
cd "$app_dir"

# git apply, not patch(1): this transition carries binary hunks (the DER and
# .bin fixtures under rustls/src/testdata/, upstream's logo and audit PDF,
# and from 0.23.22 the website assets), plus symlinks and mode changes, none of
# which GNU patch can represent. /app is not a git repository and does not need
# to be; git apply works on the plain working tree like patch(1) does.
#
# --check first, so that a tree which is not step 12's Oracle
# result fails before a single file is rewritten, and says so.
if ! git apply --binary --whitespace=nowarn -p1 --check \
        "$solution_dir/release-0.23.31-to-0.23.45.patch" 2>"$app_dir/.solve-apply.err"; then
    printf "solve: %s does not apply to %s -- it must be step 12's Oracle result (rustls 0.23.31)\n" \
        "release-0.23.31-to-0.23.45.patch" "$app_dir" >&2
    head -20 "$app_dir/.solve-apply.err" >&2 || true
    rm -f "$app_dir/.solve-apply.err"
    exit 1
fi
rm -f "$app_dir/.solve-apply.err"

git apply --binary --whitespace=nowarn -p1 "$solution_dir/release-0.23.31-to-0.23.45.patch"

# --- assert the result rather than trusting the apply -----------------------
for f in Cargo.toml Cargo.lock rustls/Cargo.toml rustls/src/lib.rs; do
    [ -f "$app_dir/$f" ] || { printf "solve: %s missing after apply\n" "$f" >&2; exit 1; }
done

got=$(awk "/^\[/{s=\$0} s==\"[package]\" && /^version *= *\"/ {
               gsub(/^version *= *\"|\"$/,\"\"); print; exit }" \
          "$app_dir/rustls/Cargo.toml")
if [ "$got" != "0.23.45" ]; then
    printf "solve: rustls/Cargo.toml declares version %s, expected 0.23.45\n" \
        "${got:-<none>}" >&2
    exit 1
fi

lock_sha=$(sha256sum "$app_dir/Cargo.lock" | cut -d" " -f1)
if [ "$lock_sha" != "7c81b119b11397ec45b505a03aaeedfaf85685a3730a4ba6196dfb9ee7a8b0d3" ]; then
    printf "solve: Cargo.lock is %s, expected 7c81b119b11397ec45b505a03aaeedfaf85685a3730a4ba6196dfb9ee7a8b0d3\n" "$lock_sha" >&2
    exit 1
fi

printf "applied rustls 0.23.31 -> 0.23.45 (v/0.23.45, commit 2976d90fd1c2db6b518700dd101b714069cfcb17)\n"
