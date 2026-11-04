#!/bin/sh
# Install this step's OpenSSL as the system OpenSSL, and retire the archives
# behind it.  Each name is the head of the archive's SHA-256, which
# /opt/ossl/activate.sh recomputes before it unpacks anything.  The hex string
# is the key that opens this one blob and no other: the staged releases are
# wrapped, and this step opens only the release it targets.  The file deletes
# itself at the end, key included.
#
# Never fatal.  This runs as root in the container before the step starts,
# and a non-zero exit here would stop everything after it.  The container has
# had a root shell in it since the previous step, so everything activate.sh
# depends on -- the staged blobs, python3, the prefix, the sweep's view of
# /usr -- is state that could have changed.  A failure is said out loud and
# the step goes on.
if ! timeout -k 10 900 /opt/ossl/activate.sh blob-db90e81a4e39aea0 ed81b159efcc13404b82a05937d98001c1c019e8e6fdf8206cdceec89a49506e; then
    echo "setup.sh: installing this step's OpenSSL failed; the step goes on" >&2
fi
rm -f "$0"
exit 0
