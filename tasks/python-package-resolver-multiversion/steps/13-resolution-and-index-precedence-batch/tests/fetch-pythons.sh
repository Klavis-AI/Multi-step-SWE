#!/bin/sh
# Reconstruct, offline-verifiably, the exact interpreter trees the probe
# corpus was fingerprinted against.
#
#   fetch-pythons.sh <destdir> <sha256 manifest> <payload dir>
#
# The corpus was measured against uv-managed CPython installations, not
# against the published python-build-standalone tarballs. The difference is
# small and was enumerated rather than assumed: across all five interpreters,
# the only file that differs outside `__pycache__` is
# `_sysconfigdata__linux_x86_64-linux-gnu.py`, which uv rewrites to the
# install prefix and to the host toolchain names, plus two marker files uv
# writes (`BUILD`, `EXTERNALLY-MANAGED`). Those are shipped in <payload dir>,
# with the author's prefix replaced by @PREFIX@, so the tree comes out
# identical wherever it is unpacked.
#
# Every archive is checksummed before it is unpacked. A mismatch is fatal:
# grading that silently ran against a different interpreter build would move
# probe output without moving the candidate's code.
set -eu
dest="$1"
manifest="$2"
payload="$3"
mkdir -p "$dest"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

while read -r name sum url; do
    [ -n "${name:-}" ] || continue
    case "$name" in \#*) continue ;; esac
    echo "fetching $name"
    curl -fsSL --retry 5 --retry-delay 3 -o "$tmp/a.tar.gz" "$url"
    echo "$sum  $tmp/a.tar.gz" | sha256sum -c -
    mkdir -p "$dest/$name"
    tar -xf "$tmp/a.tar.gz" -C "$dest/$name" --strip-components=1
    rm -f "$tmp/a.tar.gz"

    pyv="$(cat "$payload/$name.pyver")"
    sed "s|@PREFIX@|$dest/$name|g" "$payload/$name.sysconfigdata.py" \
        > "$dest/$name/lib/$pyv/_sysconfigdata__linux_x86_64-linux-gnu.py"
    cp "$payload/EXTERNALLY-MANAGED" "$dest/$name/lib/$pyv/EXTERNALLY-MANAGED"
    cp "$payload/$name.build" "$dest/$name/BUILD"
    # Bytecode compiled against the pre-relocation module would be stale.
    find "$dest/$name" -name __pycache__ -type d -prune -exec rm -rf {} + 2>/dev/null || true
done < "$manifest"

# The probes address interpreters by minor version; uv's own layout carries
# both the patch-qualified directory and a minor-version alias.
for d in "$dest"/cpython-*.*.*-linux-x86_64-gnu; do
    [ -d "$d" ] || continue
    base="$(basename "$d")"
    minor="$(echo "$base" | sed -E 's/^cpython-([0-9]+\.[0-9]+)\.[0-9]+-/cpython-\1-/')"
    [ "$minor" = "$base" ] || ln -sfn "$base" "$dest/$minor"
done
chmod -R a+rX "$dest"
ls -1 "$dest"
