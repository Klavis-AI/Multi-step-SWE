#!/usr/bin/env bash
# Warm the shared CARGO_HOME from the prefetch tree, for every toolchain the
# image has that a stand-in's own lockfile can be built with.
#
# Each prefetch directory carries the `rust-toolchain.toml` of the release it
# came from, so the channel a tree is written against is read, not guessed:
#
#   * every stand-in is fetched with the image's default toolchain, which is
#     the newest one and can read every lockfile in the ladder;
#   * a stand-in is *also* fetched with its declared channel when that channel
#     is installed, because CARGO_HOME's registry and git directories are keyed
#     by a hash of the source id that changed between cargo releases -- 1.98.1
#     reads `registry/src/index.crates.io-1949cf8c6b5b557f/`, 1.75 reads
#     `index.crates.io-6f17d22bba15001f/` -- so a cache warmed by one toolchain
#     is invisible to the other.
#
# A declared channel the image does not have is skipped rather than installed:
# the candidate cannot build with a toolchain that is not here either, and
# downloading eight more rustc releases to fetch lockfiles nobody can use would
# only cost image size.  Skipping is loud in the build log, and never silent.
#
# Each pass fetches a *copy* of the stand-in.  cargo rewrites the stand-in's
# Cargo.lock in place, and it rewrites it in the lock format of whichever
# toolchain ran: one pass with 1.98.1 leaves a version-4 lock, which cargo 1.75
# then refuses to read at all ("lock file version `4` was found, but this
# version of Cargo does not understand this lock file").  Copying keeps every
# pass looking at the lock that was shipped.
#
# Usage: fetch.sh [prefetch-root]   (default /opt/prefetch)
set -eu

root="${1:-/opt/prefetch}"
installed="$(rustup toolchain list | awk '{print $1}')"
# `rustup toolchain list` marks the default as "(active, default)", not
# "(default)", so match the word rather than a parenthesised form.
default_tc="$(rustup toolchain list | awk '/default/ {print $1; exit}')"

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

# A channel is installed if the list holds it verbatim (the default is read as
# a full triple) or under that name's triple ("1.75" -> "1.75-x86_64-...").
is_installed() {
    printf '%s\n' "$installed" | awk -v t="$1" '
        $1 == t || index($1, t "-") == 1 { found = 1 }
        END { exit !found }'
}

for dir in "$root"/*/*/; do
    declared="$(sed -n 's/^[[:space:]]*channel[[:space:]]*=[[:space:]]*"\([^"]*\)".*/\1/p' \
        "$dir/rust-toolchain.toml" 2>/dev/null || true)"
    fetched_for=""
    for tc in $default_tc $declared; do
        [ -n "$tc" ] || continue
        # A stand-in whose declared channel *is* the default (0.12.17 declares
        # 1.98.1, which the list holds as `1.98.1-x86_64-unknown-linux-gnu`)
        # would otherwise be fetched twice with the same toolchain; compare on
        # the version, since the two spellings name one toolchain.
        version="${tc%%-*}"
        case " $fetched_for " in
            *" $version "*) continue ;;
        esac
        fetched_for="$fetched_for $version"
        if ! is_installed "$tc"; then
            echo "skip $(basename "$dir"): $tc is not installed in this image"
            continue
        fi
        rm -rf "$work/stand-in"
        cp -a "$dir" "$work/stand-in"
        echo "fetching $dir with $tc"
        RUSTUP_TOOLCHAIN="$tc" cargo fetch --quiet --manifest-path "$work/stand-in/Cargo.toml"
    done
done

echo "prefetch complete: $(rustup toolchain list | wc -l) toolchains, $(ls -d "$root"/*/*/ | wc -l) stand-ins"
