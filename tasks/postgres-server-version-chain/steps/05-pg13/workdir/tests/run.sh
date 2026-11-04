#!/usr/bin/env bash
# Build the server, start it on a free port, replay the smoke corpus.
set -u
APP=${APP:-/app}
PSQL=${PSQL:-/usr/lib/postgresql/18/bin/psql}
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

make -C "$APP" || { echo "build failed"; exit 1; }
[ -x "$APP/bin/postgres" ] || { echo "no $APP/bin/postgres"; exit 1; }
[ -x "$APP/bin/initdb" ]   || { echo "no $APP/bin/initdb"; exit 1; }

PORT=$(python3 - <<'PY'
import socket
s = socket.socket(); s.bind(("127.0.0.1", 0)); print(s.getsockname()[1]); s.close()
PY
)
mkdir -p "$WORK/sock"
"$APP/bin/initdb" -D "$WORK/data" -U postgres -E UTF8 --no-locale -A trust --no-sync >/dev/null || exit 1
cat >> "$WORK/data/postgresql.conf" <<CONF
listen_addresses = '127.0.0.1'
port = $PORT
unix_socket_directories = '$WORK/sock'
timezone = 'UTC'
datestyle = 'ISO, MDY'
intervalstyle = 'postgres'
lc_messages = 'C'
client_min_messages = 'notice'
CONF
"$APP/bin/postgres" -D "$WORK/data" >"$WORK/server.log" 2>&1 &
SRV=$!
trap 'kill $SRV 2>/dev/null; rm -rf "$WORK"' EXIT
for _ in $(seq 1 60); do
    "$PSQL" -X -q -t -A -h 127.0.0.1 -p "$PORT" -U postgres -d postgres -c 'select 1' \
        >/dev/null 2>&1 && break
    sleep 0.5
done
"$PSQL" -X -q -h 127.0.0.1 -p "$PORT" -U postgres -d postgres \
    -c 'DROP DATABASE IF EXISTS smoke' -c 'CREATE DATABASE smoke' >/dev/null 2>&1

pass=0; fail=0
for f in "$APP"/tests/cases/*.sql; do
    n=$(basename "$f" .sql)
    "$PSQL" -X -a -q -h 127.0.0.1 -p "$PORT" -U postgres -d smoke < "$f" \
        > "$WORK/$n.out" 2>&1
    if cmp -s "$WORK/$n.out" "$APP/tests/expected/$n.out"; then
        pass=$((pass+1))
    else
        fail=$((fail+1))
        echo "--- $n"
        diff -u "$APP/tests/expected/$n.out" "$WORK/$n.out" | head -20
    fi
done
echo "smoke: $pass passed, $fail failed"
