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
if ! timeout -k 10 900 /opt/ossl/activate.sh blob-3ee43cfb7b1c33f5 83769c76236147fa01c48c3ca3e15a51bd6cf6612a116d6be2453801e414ba00 blob-db90e81a4e39aea0; then
    echo "setup.sh: installing this step's OpenSSL failed; the step goes on" >&2
fi
rm -f "$0"
exit 0
