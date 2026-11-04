#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
mkdir -p /app/src /app/sbin
cp -a "$ROOT/src/." /app/src/
if [ -f "$ROOT/CMakeLists.txt" ]; then
  cp "$ROOT/CMakeLists.txt" /app/CMakeLists.txt
fi
cmake -S /app -B /app/build -DCMAKE_BUILD_TYPE=Release
cmake --build /app/build -j"$(nproc)"
# Entry points named in Postfix 3.8.6 / Dovecot 2.3.21 interface.
install -m 0755 /app/sbin/mailstack /app/sbin/postfix
install -m 0755 /app/sbin/mailstack /app/sbin/dovecot
mkdir -p /app/var/spool/postfix /app/var/lib/postfix /app/var/run/dovecot /app/var/log
for u in alice bob carol mailer-daemon tlsrpt dmarc-rua; do
  mkdir -p "/app/var/mail/$u/Maildir/tmp" "/app/var/mail/$u/Maildir/new" "/app/var/mail/$u/Maildir/cur"
done
