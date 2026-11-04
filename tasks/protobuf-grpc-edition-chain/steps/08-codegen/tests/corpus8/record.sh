#!/bin/sh
# Re-record corpus8 from the upstream protoc 32.1 release binary.
#   usage: record.sh /path/to/upstream/protoc /path/to/protoc/include
# Refuses to run against a protoc without google::protobuf symbols, because the
# whole point is that expectations do not come from the reference (PROVENANCE.md).
set -u
PROTOC=${1:?usage: record.sh <upstream protoc> <include dir>}
INC=${2:?usage: record.sh <upstream protoc> <include dir>}
here=$(cd "$(dirname "$0")" && pwd)

n=$(nm -C "$PROTOC" 2>/dev/null | grep -c 'google::protobuf' || true)
[ "${n:-0}" -gt 100 ] || { echo "REFUSING: $PROTOC has $n google::protobuf symbols; that is not upstream protoc"; exit 2; }

mkdir -p "$here/expect" "$here/req"
cap=$(mktemp); cat > "$cap" <<'PY'
#!/usr/bin/env python3
import sys, os
open(os.environ["CAPTURE_OUT"], "wb").write(sys.stdin.buffer.read())
sys.stdout.buffer.write(b"")
PY
chmod 755 "$cap"

for f in "$here"/proto/*.proto; do
    stem=$(basename "$f" .proto)
    out=$(mktemp -d)
    ( cd "$here/proto" && "$PROTOC" -I. -I"$INC" --python_out="$out" "$stem.proto" ) || \
        { echo "skip $stem (did not compile)"; rm -rf "$out"; continue; }
    [ -f "$out/${stem}_pb2.py" ] && cp "$out/${stem}_pb2.py" "$here/expect/"
    rm -rf "$out"

    out=$(mktemp -d)
    CAPTURE_OUT="$here/req/$stem.req.bin" sh -c \
      "cd '$here/proto' && '$PROTOC' -I. -I'$INC' --plugin=protoc-gen-capture='$cap' --capture_out='$out' '$stem.proto'" \
      >/dev/null 2>&1
    rm -rf "$out"
    [ -s "$here/req/$stem.req.bin" ] || { rm -f "$here/req/$stem.req.bin"; continue; }
    python3 "$here/strip_sci.py" "$here/req/$stem.req.bin"
    echo "recorded $stem"
done
rm -f "$cap"
