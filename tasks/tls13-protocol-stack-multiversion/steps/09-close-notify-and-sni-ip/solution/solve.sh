#!/usr/bin/env bash
set -euo pipefail

# Oracle for step 09: apply the exact upstream release-to-release
# transition rustls 0.23.0 -> 0.23.5 over the cumulative /app
# tree left by step 08. Nothing is cloned, downloaded or fetched,
# and no cargo command is run.
#
#   upstream repository  https://github.com/rustls/rustls.git
#   upstream release     v/0.23.5 (commit 14cb5d2eac709f6c9bd46c697f090bb1f1543db1)
#   previous checkpoint  v/0.23.0 (commit eb0791bc94cfdc4c42f743902a35074ce58ced11)
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
# --check first, so that a tree which is not step 08's Oracle
# result fails before a single file is rewritten, and says so.
if ! git apply --binary --whitespace=nowarn -p1 --check \
        "$solution_dir/release-0.23.0-to-0.23.5.patch" 2>"$app_dir/.solve-apply.err"; then
    printf "solve: %s does not apply to %s -- it must be step 08's Oracle result (rustls 0.23.0)\n" \
        "release-0.23.0-to-0.23.5.patch" "$app_dir" >&2
    head -20 "$app_dir/.solve-apply.err" >&2 || true
    rm -f "$app_dir/.solve-apply.err"
    exit 1
fi
rm -f "$app_dir/.solve-apply.err"

git apply --binary --whitespace=nowarn -p1 "$solution_dir/release-0.23.0-to-0.23.5.patch"

# --- assert the result rather than trusting the apply -----------------------
for f in Cargo.toml Cargo.lock rustls/Cargo.toml rustls/src/lib.rs; do
    [ -f "$app_dir/$f" ] || { printf "solve: %s missing after apply\n" "$f" >&2; exit 1; }
done

got=$(awk "/^\[/{s=\$0} s==\"[package]\" && /^version *= *\"/ {
               gsub(/^version *= *\"|\"$/,\"\"); print; exit }" \
          "$app_dir/rustls/Cargo.toml")
if [ "$got" != "0.23.5" ]; then
    printf "solve: rustls/Cargo.toml declares version %s, expected 0.23.5\n" \
        "${got:-<none>}" >&2
    exit 1
fi

lock_sha=$(sha256sum "$app_dir/Cargo.lock" | cut -d" " -f1)
if [ "$lock_sha" != "85484447589ffd3c82d3906b97f07d954d7246039f18ab65fa7868ca8c810f8d" ]; then
    printf "solve: Cargo.lock is %s, expected 85484447589ffd3c82d3906b97f07d954d7246039f18ab65fa7868ca8c810f8d\n" "$lock_sha" >&2
    exit 1
fi

printf "applied rustls 0.23.0 -> 0.23.5 (v/0.23.5, commit 14cb5d2eac709f6c9bd46c697f090bb1f1543db1)\n"
