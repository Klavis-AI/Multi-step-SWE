#!/usr/bin/env bash
set -euo pipefail

# Oracle for step 07: apply the exact upstream release-to-release
# transition rustls 0.22.0 -> 0.22.2 over the cumulative /app
# tree left by step 06. Nothing is cloned, downloaded or fetched,
# and no cargo command is run.
#
#   upstream repository  https://github.com/rustls/rustls.git
#   upstream release     v/0.22.2 (commit 5cd41a3d4f1f0648f2e1e8e8356c19731b3360c4)
#   previous checkpoint  v/0.22.0 (commit 4d1b762b5328a1714862ba73ec72d5522fe0c049)
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
# --check first, so that a tree which is not step 06's Oracle
# result fails before a single file is rewritten, and says so.
if ! git apply --binary --whitespace=nowarn -p1 --check \
        "$solution_dir/release-0.22.0-to-0.22.2.patch" 2>"$app_dir/.solve-apply.err"; then
    printf "solve: %s does not apply to %s -- it must be step 06's Oracle result (rustls 0.22.0)\n" \
        "release-0.22.0-to-0.22.2.patch" "$app_dir" >&2
    head -20 "$app_dir/.solve-apply.err" >&2 || true
    rm -f "$app_dir/.solve-apply.err"
    exit 1
fi
rm -f "$app_dir/.solve-apply.err"

git apply --binary --whitespace=nowarn -p1 "$solution_dir/release-0.22.0-to-0.22.2.patch"

# --- assert the result rather than trusting the apply -----------------------
for f in Cargo.toml Cargo.lock rustls/Cargo.toml rustls/src/lib.rs; do
    [ -f "$app_dir/$f" ] || { printf "solve: %s missing after apply\n" "$f" >&2; exit 1; }
done

got=$(awk "/^\[/{s=\$0} s==\"[package]\" && /^version *= *\"/ {
               gsub(/^version *= *\"|\"$/,\"\"); print; exit }" \
          "$app_dir/rustls/Cargo.toml")
if [ "$got" != "0.22.2" ]; then
    printf "solve: rustls/Cargo.toml declares version %s, expected 0.22.2\n" \
        "${got:-<none>}" >&2
    exit 1
fi

lock_sha=$(sha256sum "$app_dir/Cargo.lock" | cut -d" " -f1)
if [ "$lock_sha" != "fd3f04968b3a8301e8b23b752c82bf15717c602e75fcdc4fb5397c47ada05088" ]; then
    printf "solve: Cargo.lock is %s, expected fd3f04968b3a8301e8b23b752c82bf15717c602e75fcdc4fb5397c47ada05088\n" "$lock_sha" >&2
    exit 1
fi

printf "applied rustls 0.22.0 -> 0.22.2 (v/0.22.2, commit 5cd41a3d4f1f0648f2e1e8e8356c19731b3360c4)\n"
