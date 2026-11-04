#!/usr/bin/env bash
# Starts two copies of the built server with different server names and walks a
# federation handshake between them.  It asserts nothing: read the output
# against the specification under /spec/.
set -u
ROOT="$(cd "$(dirname "$0")" && pwd)"
RUN="${RUN:-/tmp/selfcheck-fed.$$}"
rm -rf "$RUN"

start() {           # start <name> <http port> <https port>
  local d="$RUN/$1" http=$2 https=$3
  mkdir -p "$d/media_store"
  python3 - "$d" <<'PY'
import base64, sys, pathlib
run = pathlib.Path(sys.argv[1])
(run / "matrix_key.pem").write_text(
    "-----BEGIN MATRIX PRIVATE KEY-----\nKey-ID: ed25519:sc\n\n"
    + base64.b64encode(bytes(range(32))).decode() + "\n"
    "-----END MATRIX PRIVATE KEY-----\n")
PY
  openssl req -x509 -newkey rsa:2048 -nodes -keyout "$d/server.key" \
    -out "$d/server.crt" -days 365 -subj "/CN=127.0.0.1" \
    -addext "subjectAltName=IP:127.0.0.1" >/dev/null 2>&1
  cat > "$d/dendrite.yaml" <<YAML
version: 2
global:
  server_name: 127.0.0.1:$https
  private_key: $d/matrix_key.pem
  key_validity_period: 168h0m0s
  database:
    connection_string: file:$d/dendrite.db
  disable_federation: false
  presence:
    enable_inbound: true
    enable_outbound: true
client_api:
  registration_disabled: false
  guests_disabled: true
  rate_limiting:
    enabled: false
federation_api:
  disable_tls_validation: true
media_api:
  base_path: $d/media_store
  max_file_size_bytes: 10485760
logging:
  - type: std
    level: info
YAML
  "$ROOT/bin/dendrite" --config "$d/dendrite.yaml" \
    --http-bind-address "127.0.0.1:$http" \
    --https-bind-address "127.0.0.1:$https" \
    --tls-cert "$d/server.crt" --tls-key "$d/server.key" \
    > "$d/server.log" 2>&1 &
  echo $!
}

PIDA=$(start hs1 18501 18502)
PIDB=$(start hs2 18503 18504)
trap 'kill $PIDA $PIDB 2>/dev/null' EXIT
for _ in $(seq 1 60); do
  curl -sf "http://127.0.0.1:18501/_matrix/client/versions" >/dev/null \
    && curl -sf "http://127.0.0.1:18503/_matrix/client/versions" >/dev/null && break
  sleep 0.5
done

A=http://127.0.0.1:18501/_matrix/client/v3
B=http://127.0.0.1:18503/_matrix/client/v3
reg() { curl -s -X POST "$1/register" \
  -d "{\"username\":\"$2\",\"password\":\"pw-123456\",\"auth\":{\"type\":\"m.login.dummy\"}}" \
  | python3 -c 'import json,sys; print(json.load(sys.stdin).get("access_token",""))'; }
say() { echo; echo "### $*"; }

say "hs1 key document";   curl -sk "https://127.0.0.1:18502/_matrix/key/v2/server" | head -c 700; echo
say "hs2 asks hs1 for its keys (notary)"
curl -sk "https://127.0.0.1:18504/_matrix/key/v2/query/127.0.0.1:18502" | head -c 500; echo

TA=$(reg "$A" alice); TB=$(reg "$B" bob)
say "alice creates a public room on hs1"
ROOM=$(curl -s -X POST -H "Authorization: Bearer $TA" "$A/createRoom" \
  -d '{"room_version":"9","preset":"public_chat","name":"federated"}' \
  | python3 -c 'import json,sys; print(json.load(sys.stdin).get("room_id",""))')
echo "room=$ROOM"
RQ=$(python3 -c "import urllib.parse,sys;print(urllib.parse.quote(sys.argv[1],safe=''))" "$ROOM")
say "bob on hs2 joins it over federation"
curl -s -X POST -H "Authorization: Bearer $TB" "$B/join/$RQ" -d '{}'; echo
say "hs2 sees the members"
curl -s -H "Authorization: Bearer $TB" "$B/rooms/$RQ/joined_members"; echo
say "alice sends a message; hs2 should receive it"
curl -s -o /dev/null -X PUT -H "Authorization: Bearer $TA" \
  "$A/rooms/$RQ/send/m.room.message/f1" -d '{"msgtype":"m.text","body":"over federation"}'
sleep 2
curl -s -H "Authorization: Bearer $TB" "$B/rooms/$RQ/messages?dir=b&limit=3" | head -c 700; echo
say "hs1 logs";  tail -15 "$RUN/hs1/server.log"
say "hs2 logs";  tail -15 "$RUN/hs2/server.log"

# A parting reminder, because it is the one way to lose a finished milestone:
# the workspace must rebuild from source, reusing nothing compiled here.
if find /app -name '*.o' -o -name '*.a' -o -path '/app/bin/*' 2>/dev/null | grep -q .; then
    echo
    echo "NOTE: build output is still here (object files or bin/). Run 'make clean'"
    echo "      before you finish: the workspace must rebuild from"
    echo "      source, reusing nothing you compiled."
fi
