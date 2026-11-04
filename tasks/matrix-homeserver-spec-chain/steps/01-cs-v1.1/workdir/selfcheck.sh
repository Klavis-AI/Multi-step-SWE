#!/usr/bin/env bash
# Starts the server the way it is meant to be started and walks a few requests
# through it.  It asserts nothing: read the output and judge it against the
# specification under /spec/.
set -u
ROOT="$(cd "$(dirname "$0")" && pwd)"
RUN="${RUN:-/tmp/selfcheck.$$}"
PORT="${PORT:-18448}"
SPORT="${SPORT:-18449}"
rm -rf "$RUN"; mkdir -p "$RUN/media_store"

python3 - "$RUN" <<'PY'
import base64, sys, pathlib
run = pathlib.Path(sys.argv[1])
seed = bytes(range(32))
body = base64.b64encode(seed).decode()
(run / "matrix_key.pem").write_text(
    "-----BEGIN MATRIX PRIVATE KEY-----\n"
    "Key-ID: ed25519:abcdef\n\n" + body + "\n"
    "-----END MATRIX PRIVATE KEY-----\n")
PY

openssl req -x509 -newkey rsa:2048 -nodes -keyout "$RUN/server.key" \
  -out "$RUN/server.crt" -days 365 -subj "/CN=127.0.0.1" \
  -addext "subjectAltName=IP:127.0.0.1" >/dev/null 2>&1

cat > "$RUN/dendrite.yaml" <<YAML
version: 2
global:
  server_name: 127.0.0.1:$PORT
  private_key: $RUN/matrix_key.pem
  key_validity_period: 168h0m0s
  database:
    connection_string: file:$RUN/dendrite.db
  disable_federation: false
  presence:
    enable_inbound: true
    enable_outbound: true
client_api:
  registration_disabled: false
  guests_disabled: true
  registration_shared_secret: ""
  rate_limiting:
    enabled: false
federation_api:
  disable_tls_validation: true
media_api:
  base_path: $RUN/media_store
  max_file_size_bytes: 10485760
logging:
  - type: std
    level: info
YAML

"$ROOT/bin/dendrite" --config "$RUN/dendrite.yaml" \
  --http-bind-address "127.0.0.1:$PORT" \
  --https-bind-address "127.0.0.1:$SPORT" \
  --tls-cert "$RUN/server.crt" --tls-key "$RUN/server.key" \
  > "$RUN/server.log" 2>&1 &
PID=$!
trap 'kill $PID 2>/dev/null' EXIT

for _ in $(seq 1 60); do
  curl -sf "http://127.0.0.1:$PORT/_matrix/client/versions" >/dev/null && break
  sleep 0.5
done

B="http://127.0.0.1:$PORT/_matrix/client/v3"
say() { echo; echo "### $*"; }
say "GET /versions";        curl -s "http://127.0.0.1:$PORT/_matrix/client/versions"; echo
say "POST /register (challenge)"
curl -s -X POST "$B/register" -d '{"username":"alice","password":"secret-1234"}'; echo
say "POST /register (m.login.dummy)"
TOK=$(curl -s -X POST "$B/register" \
  -d '{"username":"alice","password":"secret-1234","auth":{"type":"m.login.dummy"}}' \
  | python3 -c 'import json,sys; print(json.load(sys.stdin).get("access_token",""))')
echo "access_token=${TOK:0:12}..."
say "GET /account/whoami"; curl -s -H "Authorization: Bearer $TOK" "$B/account/whoami"; echo
say "POST /createRoom";
ROOM=$(curl -s -X POST -H "Authorization: Bearer $TOK" "$B/createRoom" \
  -d '{"room_version":"6","preset":"public_chat","name":"selfcheck"}' \
  | python3 -c 'import json,sys; print(json.load(sys.stdin).get("room_id",""))')
echo "room=$ROOM"
ROOMQ=$(python3 -c "import urllib.parse,sys;print(urllib.parse.quote(sys.argv[1],safe=''))" "$ROOM")
say "PUT /rooms/.../send/m.room.message/1"
curl -s -X PUT -H "Authorization: Bearer $TOK" \
  "$B/rooms/$ROOMQ/send/m.room.message/txn1" \
  -d '{"msgtype":"m.text","body":"hello"}'; echo
say "GET /rooms/.../state"; curl -s -H "Authorization: Bearer $TOK" "$B/rooms/$ROOMQ/state" | head -c 600; echo
say "GET /rooms/.../messages"; curl -s -H "Authorization: Bearer $TOK" \
  "$B/rooms/$ROOMQ/messages?dir=b&limit=5" | head -c 600; echo
say "GET /sync"; curl -s -H "Authorization: Bearer $TOK" "$B/sync?timeout=0" | head -c 800; echo
say "POST /_matrix/media/v3/upload"
MXC=$(printf 'hello media' | curl -s -X POST -H "Authorization: Bearer $TOK" \
  -H 'Content-Type: text/plain' --data-binary @- \
  "http://127.0.0.1:$PORT/_matrix/media/v3/upload?filename=hello.txt" \
  | python3 -c 'import json,sys; print(json.load(sys.stdin).get("content_uri",""))')
echo "mxc=$MXC"
if [ -n "$MXC" ]; then
  MID=${MXC##*/}
  say "GET /_matrix/media/v3/download"
  curl -s -i "http://127.0.0.1:$PORT/_matrix/media/v3/download/127.0.0.1:$PORT/$MID" | head -12
fi
say "server log (tail)"; tail -20 "$RUN/server.log"

# A parting reminder, because it is the one way to lose a finished milestone:
# the workspace must rebuild from source, reusing nothing compiled here.
if find /app -name '*.o' -o -name '*.a' -o -path '/app/bin/*' 2>/dev/null | grep -q .; then
    echo
    echo "NOTE: build output is still here (object files or bin/). Run 'make clean'"
    echo "      before you finish: the workspace must rebuild from"
    echo "      source, reusing nothing you compiled."
fi
