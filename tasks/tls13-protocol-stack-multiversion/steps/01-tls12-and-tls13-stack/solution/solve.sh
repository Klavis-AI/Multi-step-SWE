#!/usr/bin/env bash
set -euo pipefail

# Oracle for step 01: install upstream rustls 0.19.0 over the
# starter scaffold. Nothing is cloned, downloaded or fetched; the payload
# beside this script is the whole input, and no cargo command is run.
#
#   upstream repository  https://github.com/rustls/rustls.git
#   upstream release     v/0.19.0 (commit 47d309233f2d21ccf058d35c58d684e5e4c9c643)
#   licence              Apache-2.0 OR ISC OR MIT (see LICENSE beside this file)
#   excluded             test-ca bogo/bogo target -- verifier-owned or build output
#
# Upstream committed no Cargo.lock at this release, so the archive carries
# none and the frozen lockfile is installed separately below. That keeps the
# archive byte-identical to `git archive v/0.19.0` with test-ca bogo/bogo target removed.

solution_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
app_dir=${HARBOR_APP_DIR:-/app}

# Verify the payload before touching the workspace. Loudly: a corrupted
# archive that is unpacked anyway leaves a half-upstream tree that fails much
# later, somewhere that does not name this file.
if ! ( cd "$solution_dir" && sha256sum -c --quiet --strict SHA256SUMS ); then
    printf "solve: payload checksum mismatch under %s; %s not touched\n" \
        "$solution_dir" "$app_dir" >&2
    exit 1
fi

[ -d "$app_dir" ] || { printf "solve: no %s\n" "$app_dir" >&2; exit 1; }
cd "$app_dir"

# Remove the starter's copy of every path the archive owns, then unpack. A
# plain overlay would also work -- every starter path exists upstream -- but
# removing first makes the result equal to the archive by construction instead
# of by an argument about the starter, so a future starter that grew a file
# cannot leave a stale one behind. Only these names are touched: the workdir's
# own TEST_INTERFACE.md, WORKSPACE_HANDOFF.md, timer.sh and docs/ are not in
# the list and survive, and neither is test-ca/, which the verifier owns.
owned=".azure-pipelines.yml .github .gitignore .rustfmt.toml .travis.yml CONTRIBUTING.md Cargo.toml LICENSE LICENSE-APACHE LICENSE-ISC LICENSE-MIT OLDCHANGES.md README.md SECURITY.md admin audit bogo fuzz rustls rustls-mio trytls"
set -f            # these are literal names, never globs
for entry in $owned; do
    rm -rf -- "./$entry"
done
set +f

tar -xzf "$solution_dir/release-0.19.0.tar.gz" -C "$app_dir"

# Upstream shipped no lockfile at this release and the verifier builds with
# `cargo build --locked`, so install the frozen resolve the step was measured
# against.
cp -- "$solution_dir/Cargo.lock" "$app_dir/Cargo.lock"
chmod 0644 "$app_dir/Cargo.lock"

# --- assert the result rather than trusting the extraction ------------------
for f in Cargo.toml Cargo.lock rustls/Cargo.toml rustls/src/lib.rs; do
    [ -f "$app_dir/$f" ] || { printf "solve: %s missing after unpack\n" "$f" >&2; exit 1; }
done

# `version = ` also appears under [dependencies], so isolate [package] first.
got=$(awk "/^\[/{s=\$0} s==\"[package]\" && /^version *= *\"/ {
               gsub(/^version *= *\"|\"$/,\"\"); print; exit }" \
          "$app_dir/rustls/Cargo.toml")
if [ "$got" != "0.19.0" ]; then
    printf "solve: rustls/Cargo.toml declares version %s, expected 0.19.0\n" \
        "${got:-<none>}" >&2
    exit 1
fi

lock_sha=$(sha256sum "$app_dir/Cargo.lock" | cut -d" " -f1)
if [ "$lock_sha" != "ff5d957ddb0168195fe9b631c7c4bc328fa0c05a06179a0af3682b9c6a805e60" ]; then
    printf "solve: Cargo.lock is %s, expected ff5d957ddb0168195fe9b631c7c4bc328fa0c05a06179a0af3682b9c6a805e60\n" "$lock_sha" >&2
    exit 1
fi

printf "installed rustls 0.19.0 from v/0.19.0 (commit 47d309233f2d21ccf058d35c58d684e5e4c9c643)\n"
