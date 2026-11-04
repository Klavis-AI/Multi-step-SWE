#!/usr/bin/env bash
# fetch.sh <id> <dest-dir> -- install one pinned toolchain component.
#
# Called from a Dockerfile RUN. Reads pins.tsv from its own directory, so the
# URL and the checksum live in one file that every image shares and no
# Dockerfile repeats a version number it could get wrong.
#
# The checksum is the point. archive.apache.org is a live service; a component
# that comes down changed is a silently different measurement from the one the
# version sweep recorded, and the sweep is what every expectation in this task
# is calibrated against.
#
# The other thing archive.apache.org is, is slow. Measured from this box on
# 17 Sep, one connection to it sustains 193-225 KB/s no matter what is asked
# for, which is 20 minutes for a 236 MB Spark distribution and more than an
# image build can afford to spend on one file. It is a per-connection limit
# rather than a per-host one: six ranged GETs of the same file, at the same
# moment, summed to 748 KB/s. So a large file is fetched in parallel pieces
# and reassembled, and the pinned sha256 below is what decides whether the
# reassembly was right. Nothing about the bytes that are accepted changes;
# only how long they take to arrive.
set -euo pipefail

ID=${1:?usage: fetch.sh <id> <dest-dir>}
DEST=${2:?usage: fetch.sh <id> <dest-dir>}
HERE=$(cd "$(dirname "$0")" && pwd)

line=$(awk -F'\t' -v id="$ID" '$1 == id {print; exit}' "$HERE/pins.tsv")
[ -n "$line" ] || { echo "fetch: no pin named '$ID' in pins.tsv" >&2; exit 2; }
url=$(printf '%s' "$line" | cut -f2)
want=$(printf '%s' "$line" | cut -f3)

# --- fetching a large file from a per-connection rate limiter ------------------
# Six ranged GETs at once, reassembled in order. Returns non-zero -- and leaves
# the caller to fall back to one plain stream -- on anything it is not sure
# about: a server that does not advertise byte ranges, a length it cannot read,
# a file small enough that six connections are not worth the risk, a chunk that
# failed, or a chunk that came back the wrong size. The last of those is the one
# that matters: a short chunk concatenates into a plausible file, so the size of
# every piece is checked before they are joined and the sha256 is checked after.
CHUNKS=6
CHUNK_MIN=33554432                   # 32 MiB; below this one stream is fine

fetch_parallel() {
    local url=$1 out=$2
    local head len ranges d i pid rc=0
    head=$(curl -fsSIL --max-time 120 "$url" 2>/dev/null) || return 1
    # Last response wins: with -L the headers of every hop are printed in turn.
    len=$(printf '%s' "$head" | tr -d '\r' \
          | awk 'tolower($1) == "content-length:" {v = $2} END {print v + 0}')
    ranges=$(printf '%s' "$head" | tr -d '\r' \
          | awk 'tolower($1) == "accept-ranges:" {v = tolower($2)} END {print v}')
    [ "$ranges" = bytes ] || return 1
    [ "$len" -ge "$CHUNK_MIN" ] 2>/dev/null || return 1

    d=$(mktemp -d /tmp/fetch-parts-XXXXXX)
    local span=$(( (len + CHUNKS - 1) / CHUNKS ))
    local -a pids=()
    for ((i = 0; i < CHUNKS; i++)); do
        local s=$((i * span)) e=$(((i + 1) * span - 1))
        [ "$e" -ge "$len" ] && e=$((len - 1))
        curl -fsSL --retry 3 --retry-delay 5 --retry-connrefused \
             --speed-limit 1024 --speed-time 120 --max-time 1800 \
             -r "$s-$e" "$url" -o "$d/part.$(printf '%02d' "$i")" &
        pids+=($!)
    done
    for pid in "${pids[@]}"; do wait "$pid" || rc=1; done
    if [ "$rc" = 0 ]; then
        local total=0
        for ((i = 0; i < CHUNKS; i++)); do
            local s=$((i * span)) e=$(((i + 1) * span - 1))
            [ "$e" -ge "$len" ] && e=$((len - 1))
            local want_n=$((e - s + 1)) got_n
            got_n=$(wc -c < "$d/part.$(printf '%02d' "$i")" 2>/dev/null || echo -1)
            [ "$got_n" = "$want_n" ] || { rc=1; break; }
            total=$((total + got_n))
        done
        [ "$rc" = 0 ] && [ "$total" = "$len" ] || rc=1
    fi
    if [ "$rc" = 0 ]; then
        cat "$d"/part.* > "$out" || rc=1
    fi
    rm -rf "$d"
    [ "$rc" = 0 ] && printf 'fetch: %s in %d parallel piece(s), %d bytes\n' \
        "${url##*/}" "$CHUNKS" "$len" >&2
    return "$rc"
}

tgz=$(mktemp /tmp/fetch-XXXXXX.tgz)
trap 'rm -f "$tgz"' EXIT
# TOOLCHAIN_MIRROR is an authoring convenience and nothing more. Empty -- which
# is what every shipped image builds with -- and this is exactly the pinned
# URL. Set, it is tried first by basename, and the checksum below still decides
# whether the bytes are accepted: a mirror that serves something else fails the
# same way archive.apache.org would. It exists because a rebuild off
# archive.apache.org means re-pulling a few hundred megabytes through a rate
# limiter for bytes that are already on the disk, byte-for-byte checked.
#
# --retry because archive.apache.org is rate-limited often enough to matter over
# a build that pulls a 225 MB distribution.
got=
if [ -n "${TOOLCHAIN_MIRROR:-}" ]; then
    if curl -fsS --max-time 600 "$TOOLCHAIN_MIRROR/${url##*/}" -o "$tgz"; then
        got=$(sha256sum "$tgz" | cut -d' ' -f1)
        [ "$got" = "$want" ] || { echo "fetch: $ID mirror copy is not the pinned bytes; falling back" >&2; got=; }
    fi
fi
if [ -z "$got" ] && fetch_parallel "$url" "$tgz"; then
    got=$(sha256sum "$tgz" | cut -d' ' -f1)
    [ "$got" = "$want" ] || { echo "fetch: $ID parallel copy is not the pinned bytes; falling back" >&2; got=; }
fi
if [ -z "$got" ]; then
    curl -fsSL --retry 5 --retry-delay 5 --retry-connrefused "$url" -o "$tgz"
    got=$(sha256sum "$tgz" | cut -d' ' -f1)
fi
if [ "$got" != "$want" ]; then
    echo "fetch: $ID checksum mismatch" >&2
    echo "fetch:   url  $url" >&2
    echo "fetch:   want $want" >&2
    echo "fetch:   got  $got" >&2
    exit 3
fi

mkdir -p "$DEST"
tar -xzf "$tgz" -C "$DEST" --strip-components=1
printf 'fetch: %s -> %s (%s bytes, sha256 ok)\n' "$ID" "$DEST" "$(wc -c < "$tgz")"
