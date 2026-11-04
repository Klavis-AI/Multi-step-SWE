#!/usr/bin/env bash
# Regenerate corpus/proto2/MANIFEST.tsv and corpus/reject/MANIFEST.tsv.
# Descriptions come from each file's "// DESC:" header; reject exit codes are
# the REAL exit status observed from protoc, never assumed.
set -u
CORPUS="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROTOC="${PROTOC:-$CORPUS/../protoc/v32.1/bin/protoc}"

desc_of() { sed -n 's|^// DESC: *||p' "$1" | head -1 | tr '\t' ' '; }

# ---- proto2: stratum <TAB> relative_path <TAB> description ----
cd "$CORPUS/proto2"
: > MANIFEST.tsv
for f in $(find . -name '*.proto' | sed 's|^\./||' | sort); do
  printf '%s\t%s\t%s\n' "$(dirname "$f")" "$f" "$(desc_of "$f")" >> MANIFEST.tsv
done

# ---- reject: relative_path <TAB> what is wrong <TAB> observed exit code ----
cd "$CORPUS/reject"
: > MANIFEST.tsv
for f in $(find . -name '*.proto' | sed 's|^\./||' | sort); do
  d="$(desc_of "$f")"
  "$PROTOC" -I. --descriptor_set_out=/dev/null "$f" >/dev/null 2>&1
  rc=$?                                   # captured immediately, before any other command
  printf '%s\t%s\t%s\n' "$f" "$d" "$rc" >> MANIFEST.tsv
done
