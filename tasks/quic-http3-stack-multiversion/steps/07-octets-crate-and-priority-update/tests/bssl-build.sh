#!/usr/bin/env bash
# Build one pinned BoringSSL into the layout quiche's build script expects.
#
# Usage: bssl-build.sh <commit-sha> <destdir>
#
# The candidate's tree never contains BoringSSL. Upstream carries it as a git
# submodule (`deps/boringssl`, then `quiche/deps/boringssl` from 0.11.0) and
# builds it from source through cmake at every `cargo build`; neither the
# submodule content nor a cmake run is available to a verifier that has no
# network and one hour. So the library is built here, at image-build time, from
# the exact commit that release pinned, and handed to the build script through
# the escape hatch upstream already provides: `QUICHE_BSSL_PATH`.
#
# Pinning is by commit SHA rather than by archive checksum on purpose: a git
# commit SHA is a hash over the tree it names, so fetching it *is* the integrity
# check, and no separate digest can go stale against it.
set -euo pipefail
sha=$1
dest=$2
src=$(mktemp -d)

git -c init.defaultBranch=main init -q "$src"
git -C "$src" remote add origin https://github.com/google/boringssl.git
git -C "$src" fetch -q --depth 1 origin "$sha"
git -C "$src" -c advice.detachedHead=false checkout -q FETCH_HEAD
got=$(git -C "$src" rev-parse HEAD)
[ "$got" = "$sha" ] || { echo "BoringSSL pin mismatch: wanted $sha, got $got" >&2; exit 1; }

# -Wno-error: these commits span 2019 to 2024 and are compiled here by one
#   toolchain none of them was written for, so their own -Werror would fail the
#   image build on warnings that say nothing about the candidate. The 2019 pin
#   really does fail this way -- gcc 12 reports a false-positive array bounds
#   warning in bn_sqr_comba8.
#
#   It has to go in CMAKE_<LANG>_FLAGS_RELEASE, not CMAKE_<LANG>_FLAGS:
#   BoringSSL appends -Werror to CMAKE_C_FLAGS itself, from inside its own
#   CMakeLists.txt, which overwrites anything passed on the command line. The
#   per-configuration flags are appended after that, so they win. (Measured:
#   -DCMAKE_C_FLAGS=-Wno-error alone does not stop the failure.) The Release
#   defaults are restated because setting the variable replaces them.
# POSITION_INDEPENDENT_CODE: quiche produces a cdylib (libquiche.so); linking a
#   non-PIC static archive into it fails at the last step of a long build with
#   `relocation R_X86_64_PC32 against symbol 'stderr'`.
cmake -S "$src" -B "$src/build" \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_SHARED_LIBS=OFF \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
    -DCMAKE_C_FLAGS_RELEASE="-O3 -DNDEBUG -Wno-error" \
    -DCMAKE_CXX_FLAGS_RELEASE="-O3 -DNDEBUG -Wno-error" >/dev/null
cmake --build "$src/build" --target crypto ssl -j 4 >/dev/null

# Two layouts, because the build script's expectation moved. 0.1.0 reads
# `$P/build/crypto/libcrypto.a` and `$P/build/ssl/libssl.a`; 0.11.0 onward reads
# `$P/build/libcrypto.a`; 0.18.0 onward falls back to `$P/libcrypto.a` when
# `$P/build` is not a directory. Populating all three costs two hard links and
# removes a whole class of per-step breakage.
mkdir -p "$dest/build/crypto" "$dest/build/ssl"
libc=$(find "$src/build" -name libcrypto.a -print -quit)
libs=$(find "$src/build" -name libssl.a -print -quit)
[ -n "$libc" ] && [ -n "$libs" ] || { echo "BoringSSL build produced no archives" >&2; exit 1; }
cp "$libc" "$dest/build/libcrypto.a"
cp "$libs" "$dest/build/libssl.a"
ln "$dest/build/libcrypto.a" "$dest/build/crypto/libcrypto.a"
ln "$dest/build/libssl.a"    "$dest/build/ssl/libssl.a"
cp "$dest/build/libcrypto.a" "$dest/libcrypto.a"
cp "$dest/build/libssl.a"    "$dest/libssl.a"
# Two source layouts as well. The 2019-2020 pins are plain `master` checkouts
# with headers at `include/openssl`. The 2021-2024 pins are `master-with-bazel`
# commits -- the branch BoringSSL publishes with pre-generated assembly and a
# Bazel build -- which keeps the real tree under `src/`, headers included. Both
# build the same two archives from the same top-level CMakeLists.txt; only the
# header root moves.
inc="$src/include"
[ -d "$inc" ] || inc="$src/src/include"
[ -d "$inc" ] || { echo "BoringSSL checkout has no include/ at either layout" >&2; exit 1; }
cp -r "$inc" "$dest/include"
[ -f "$dest/include/openssl/ssl.h" ] || { echo "BoringSSL headers landed wrong: no openssl/ssl.h" >&2; exit 1; }
printf '%s\n' "$sha" > "$dest/COMMIT"

rm -rf -- "$src"
echo "BoringSSL $sha -> $dest ($(du -sh "$dest" | cut -f1))"
