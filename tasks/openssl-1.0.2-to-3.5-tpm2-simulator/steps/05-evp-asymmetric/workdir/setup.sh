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
if ! timeout -k 10 900 /opt/ossl/activate.sh blob-933a73adb2afc9eb 9d0f5ae374ff6143860d437ebb33dbcb859be1c7c747e3558e6028f1c6230a4c blob-db90e81a4e39aea0 blob-3ee43cfb7b1c33f5; then
    echo "setup.sh: installing this step's OpenSSL failed; the step goes on" >&2
fi
rm -f "$0"
exit 0
