#!/bin/sh
# Print the OpenSSL release the loader actually binds, not the one a label says.
#
# Compiling and running is the only way to answer this: `openssl version` reads
# a binary that may not be the one a build links, a header says what was
# compiled against and not what was loaded, and ldconfig lists candidates
# without saying which wins.  A program that prints OpenSSL_version() after the
# loader has chosen says all three at once.
set -e
d=$(mktemp -d)
trap 'rm -rf "$d"' EXIT
cat > "$d/p.c" <<'EOF'
#include <stdio.h>
#include <openssl/crypto.h>
int main(void) { printf("%s\n", OpenSSL_version(OPENSSL_VERSION)); return 0; }
EOF
gcc -o "$d/p" "$d/p.c" -lcrypto >/dev/null 2>&1
"$d/p" | awk '{print $2}'
