#!/usr/bin/env bash
#
# record.sh -- regenerate every recorded expectation for the proto2 corpus.
#
# The rule this script exists to enforce: *protoc produces every expected byte*.
# This script only chooses which files and messages to feed it, and where the
# output lands. No expected descriptor or encoded message is ever hand-written.
#
# The script is a full rebuild (it deletes corpus/expect first), so running it
# twice must produce byte-identical output. See corpus/PROVENANCE.md.
#
# Env overrides: PROTOC=<path>  CODEC_TARGETS=<n>
set -euo pipefail

CORPUS="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROTOC="${PROTOC:-$CORPUS/../protoc/v32.1/bin/protoc}"
ROOT="$CORPUS/proto2"
EXPECT="$CORPUS/expect"
TOOLS="$CORPUS/tools"
WORK="$CORPUS/_record_work"
CODEC_TARGETS="${CODEC_TARGETS:-40}"

[ -x "$PROTOC" ] || { echo "protoc not executable: $PROTOC" >&2; exit 2; }
echo "# protoc: $PROTOC ($("$PROTOC" --version))"

rm -rf "$EXPECT" "$WORK"
mkdir -p "$EXPECT/desc" "$EXPECT/descfull" \
         "$EXPECT/codec/in" "$EXPECT/codec/encode" \
         "$EXPECT/codec/decode" "$EXPECT/codec/decode_raw" "$WORK"

cd "$ROOT"
# LC_ALL=C keeps the ordering locale-independent, which idempotency depends on.
FILES="$(find . -name '*.proto' | sed 's|^\./||' | LC_ALL=C sort)"

# --------------------------------------------------------------------------
# Family 1: plain descriptors   Family 2: full descriptors
# --------------------------------------------------------------------------
echo "# recording descriptors"
for f in $FILES; do
  d="$(dirname "$f")"
  mkdir -p "$EXPECT/desc/$d" "$EXPECT/descfull/$d"
  "$PROTOC" -I. -I"$ROOT" --descriptor_set_out="$EXPECT/desc/$f.bin" "$f"
  "$PROTOC" -I. -I"$ROOT" --include_imports --include_source_info --retain_options \
            --descriptor_set_out="$EXPECT/descfull/$f.bin" "$f"
done

# --------------------------------------------------------------------------
# Family 3: codec
#
# Codec *inputs* are derived mechanically from the descriptors protoc just
# emitted: the full descriptor set is rendered to text by protoc itself, and
# tools/gen_textproto.py walks that text to choose placeholder field values.
# --------------------------------------------------------------------------
echo "# rendering descriptors to text (input derivation only)"
for f in $FILES; do
  mkdir -p "$WORK/txt/$(dirname "$f")"
  "$PROTOC" --decode=google.protobuf.FileDescriptorSet \
            google/protobuf/descriptor.proto \
            < "$EXPECT/descfull/$f.bin" > "$WORK/txt/$f.txt"
done

echo "# enumerating candidate messages"
: > "$WORK/candidates.tsv"
for f in $FILES; do
  st="$(dirname "$f")"
  python3 "$TOOLS/gen_textproto.py" --list "$WORK/txt/$f.txt" "$f" \
    | while read -r m; do printf '%s\t%s\t%s\n' "$st" "$f" "${m#.}"; done \
    >> "$WORK/candidates.tsv"
done

# Keep only messages whose generated instance actually encodes to a non-empty
# message; extension-range hosts and service-only holders encode to zero bytes
# and make worthless codec expectations.
echo "# probing which candidates encode"
: > "$WORK/nonempty.tsv"
while IFS=$'\t' read -r st f msg; do
  python3 "$TOOLS/gen_textproto.py" --emit "$WORK/txt/$f.txt" "$msg" \
      > "$WORK/probe.txt" 2>/dev/null || continue
  "$PROTOC" -I. -I"$ROOT" --encode="$msg" "$f" \
      < "$WORK/probe.txt" > "$WORK/probe.bin" 2>/dev/null || continue
  [ -s "$WORK/probe.bin" ] || continue
  printf '%s\t%s\t%s\n' "$st" "$f" "$msg" >> "$WORK/nonempty.tsv"
done < "$WORK/candidates.tsv"

# Round-robin across strata so the sample spreads instead of exhausting one
# directory. awk (not head) does the truncation: head would SIGPIPE the
# generator and trip pipefail.
LC_ALL=C sort "$WORK/nonempty.tsv" \
  | python3 "$TOOLS/pick_codec_targets.py" \
  | awk -v n="$CODEC_TARGETS" 'NR<=n' > "$WORK/targets.tsv"

echo "# recording codec expectations for $(wc -l < "$WORK/targets.tsv") messages"
MAN="$EXPECT/codec/MANIFEST.tsv"
: > "$MAN"
while IFS=$'\t' read -r st f msg; do
  # Fully-qualified message names are unique corpus-wide, so they are the slug.
  python3 "$TOOLS/gen_textproto.py" --emit "$WORK/txt/$f.txt" "$msg" \
      > "$EXPECT/codec/in/$msg.txt"
  "$PROTOC" -I. -I"$ROOT" --encode="$msg" "$f" \
      < "$EXPECT/codec/in/$msg.txt" > "$EXPECT/codec/encode/$msg.bin"
  "$PROTOC" -I. -I"$ROOT" --decode="$msg" "$f" \
      < "$EXPECT/codec/encode/$msg.bin" > "$EXPECT/codec/decode/$msg.txt"
  "$PROTOC" --decode_raw \
      < "$EXPECT/codec/encode/$msg.bin" > "$EXPECT/codec/decode_raw/$msg.txt"
  printf 'encode\t%s\t%s\tin/%s.txt\tencode/%s.bin\n'         "$f" "$msg" "$msg" "$msg" >> "$MAN"
  printf 'decode\t%s\t%s\tencode/%s.bin\tdecode/%s.txt\n'     "$f" "$msg" "$msg" "$msg" >> "$MAN"
  printf 'decode_raw\t%s\t%s\tencode/%s.bin\tdecode_raw/%s.txt\n' "$f" "$msg" "$msg" "$msg" >> "$MAN"
done < "$WORK/targets.tsv"

# --------------------------------------------------------------------------
# Checksum sweep over everything recorded (this file excluded from itself).
# --------------------------------------------------------------------------
cd "$EXPECT"
find desc descfull codec -type f | LC_ALL=C sort | xargs sha256sum > CHECKSUMS.sha256

rm -rf "$WORK"
echo "# files recorded: $(find "$EXPECT" -type f | wc -l)"
echo "# bytes recorded: $(find "$EXPECT" -type f -printf '%s\n' | paste -sd+ | bc)"
