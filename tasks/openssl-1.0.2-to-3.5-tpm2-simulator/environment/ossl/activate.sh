#!/bin/sh
# Install one staged OpenSSL build under /usr/local, replacing whatever is there.
#
#     activate.sh <blob-id> <key-hex> [<blob-id to retire> ...]
#
# The releases staged in this directory were configured with
# --prefix=/usr/local --openssldir=/usr/local/ssl --libdir=lib and packed from
# their DESTDIR, so unpacking one at "/" makes it the system OpenSSL: the
# compiler's default include path finds its headers, pkg-config and CMake's
# FindOpenSSL find its .pc and config files, the dynamic loader finds its
# shared objects, and `openssl version` reports it.  No environment variables
# are involved, and nothing outside /usr/local is touched.
#
# An archive here is named for its contents and not for its release: a blob id
# is "blob-" followed by the first 16 hex digits of the archive's SHA-256, and
# this script recomputes that digest before it unpacks anything.  The name is a
# claim the file has to satisfy, so an archive that has been swapped, truncated
# or rebuilt fails here rather than becoming the system OpenSSL quietly.  It
# also means there is no list of releases in this file and no version argument
# to it: /opt/ossl holds several staged releases in one directory, and this
# script names none of them.  A step is told which OpenSSL it targets by its
# own instruction.
#
# A hashed name only keeps the release out of `ls`, so each blob is
# also wrapped under a key of its own (see osslwrap.py) and the key is the
# second argument.  Each step's setup.sh carries exactly one: the key to the
# release that step targets.  The digest check is what
# catches a wrong key -- it is applied to what comes out of the unwrap, so a
# bad key and a substituted archive fail the same test at the same place,
# before the prefix is touched.
#
# Exactly one release is ever *installed*: the prefix is cleared before
# unpacking.  Every blob named after the first argument is deleted -- a release
# the migration has already moved past is a retired toolchain, not a fallback,
# and leaving one on disk would let code that has not been migrated keep
# building.  Which ones those are is decided by the caller, not by this script.
set -e

ID=${1:?usage: activate.sh <blob-id> <key-hex> [<blob-id to retire> ...]}
KEY=${2:?usage: activate.sh <blob-id> <key-hex> [<blob-id to retire> ...]}
shift 2

# The shape of the id is checked before it is used as a path: everything below
# depends on the name being a digest prefix, and a name that is not one could
# not be checked against anything.
case "$ID" in
    blob-*) ;;
    *) echo "activate.sh: '$ID' is not a blob id" >&2; exit 1 ;;
esac
BIN=/opt/ossl/$ID.bin
if [ ! -f "$BIN" ]; then
    echo "activate.sh: $BIN is not staged" >&2
    exit 1
fi

# The key's shape too, and for the same reason: osslwrap.py will refuse a key
# that is not 64 hex digits, but the message it gives is about a key, and the
# one thing a caller can mistype here that looks like a key is a truncated one.
case "$KEY" in
    *[!0-9a-fA-F]*|"") echo "activate.sh: the key is not hex" >&2; exit 1 ;;
esac
if [ ${#KEY} -ne 64 ]; then
    echo "activate.sh: a key is 64 hex digits, not ${#KEY}" >&2
    exit 1
fi

# Unwrap into a temporary file, and hash *that*.  The blob id names the digest
# of the archive, not of the wrapped file, so this one check covers both things
# that can be wrong: a blob that is not what its name claims, and a key that
# does not open it.  Neither reaches the prefix -- nothing below this point has
# run yet.
TAR=$(mktemp /tmp/ossl-XXXXXX.tar.xz)
trap 'rm -f "$TAR"' EXIT HUP INT TERM
python3 /opt/ossl/osslwrap.py unwrap "$BIN" "$TAR" "$KEY"

want=${ID#blob-}
got=$(sha256sum "$TAR" | cut -d' ' -f1)
case "$got" in
    "$want"*) ;;
    *) echo "activate.sh: $BIN does not unwrap to $ID under this key" \
            "(got $got); nothing has been installed" >&2; exit 1 ;;
esac

# The retire list is checked here, before anything is removed or unpacked.  A
# name this script would refuse is a caller's mistake either way, and refusing
# it after the prefix has already been replaced would leave the container with
# a half-done install and a non-zero exit -- the same complaint, arriving too
# late to be acted on.
for r in "$@"; do
    case "$r" in
        blob-*) ;;
        *) echo "activate.sh: '$r' is not a blob id" >&2; exit 1 ;;
    esac
    if [ "$r" = "$ID" ]; then
        echo "activate.sh: asked to retire $ID, which is what it installs" >&2
        exit 1
    fi
done

rm -rf /usr/local/include/openssl \
       /usr/local/ssl \
       /usr/local/lib/ossl-modules \
       /usr/local/lib/cmake/OpenSSL
rm -rf /usr/local/lib/engines-1.1 /usr/local/lib/engines-3
rm -f  /usr/local/lib/libcrypto.so* /usr/local/lib/libssl.so* \
       /usr/local/lib/libcrypto.a /usr/local/lib/libssl.a \
       /usr/local/lib/pkgconfig/libcrypto.pc \
       /usr/local/lib/pkgconfig/libssl.pc \
       /usr/local/lib/pkgconfig/openssl.pc \
       /usr/local/bin/openssl /usr/local/bin/c_rehash

# And take away every other OpenSSL a compiler could find.
#
# The image is built without libssl-dev -- see the header of
# environment/Dockerfile -- but it does not stay that way.  Before the first
# step's setup.sh, tooling is installed into the running container with
#
#     apt-get update && apt-get install -y curl bash nodejs npm procps
#
# and bookworm's npm pulls node-gyp, which pulls libssl-dev.  That plants a
# complete second OpenSSL development tree: 133 headers of Debian's 3.0.20
# under /usr/include/openssl, a static libcrypto.a, unversioned
# libcrypto.so/libssl.so linker symlinks, and three .pc files.  gcc, ld and
# pkg-config all search /usr/local before /usr, so the default still resolves
# to the release installed here -- but "this machine has OpenSSL <v> installed"
# is a sentence in every step's instruction, and it has to be a fact about the
# machine rather than a search order.  At steps 3 to 5 the intruder is the same
# 3.0 series the step targets and carries the same soname, where a search order
# is the only thing that would have separated them at all.
#
# Only the development half goes.  The versioned runtime objects stay -- git,
# curl and node are linked against them -- and they are reachable by soname and
# by nothing else, which the ld.so.conf.d drop-in in the Dockerfile settles in
# /usr/local's favour.  This runs at the top of every step, not once, because
# the previous step had a root shell in this container and a network.
swept=
for p in /usr/include/openssl /usr/include/*/openssl; do
    [ -e "$p" ] || continue
    rm -rf "$p"
    swept="$swept $p"
done
for p in /usr/lib/libcrypto.a  /usr/lib/libssl.a \
         /usr/lib/libcrypto.so /usr/lib/libssl.so \
         /usr/lib/*/libcrypto.a  /usr/lib/*/libssl.a \
         /usr/lib/*/libcrypto.so /usr/lib/*/libssl.so \
         /usr/lib/pkgconfig/libcrypto.pc /usr/lib/pkgconfig/libssl.pc \
         /usr/lib/pkgconfig/openssl.pc \
         /usr/lib/*/pkgconfig/libcrypto.pc /usr/lib/*/pkgconfig/libssl.pc \
         /usr/lib/*/pkgconfig/openssl.pc; do
    [ -e "$p" ] || continue
    rm -f "$p"
    swept="$swept $p"
done
if [ -n "$swept" ]; then
    echo "activate.sh: removed an OpenSSL development tree outside /usr/local:$swept"
fi

tar -xJf "$TAR" -C /
rm -f "$TAR"
trap - EXIT HUP INT TERM
ldconfig

# Assert the sweep, because a sweep that matched nothing reads exactly like a
# sweep that worked.  /app is excluded by where this looks rather than by a
# rule: the repository vendors an OpenSSL 1.0.2 and removing it is step 1's
# job, not this script's.
stray=$(find /usr -xdev \
        \( -name opensslv.h -o -name libcrypto.so -o -name libssl.so \
           -o -name libcrypto.a -o -name libssl.a -o -name libcrypto.pc \) \
        -not -path '/usr/local/*' 2>/dev/null | tr '\n' ' ')
if [ -n "$stray" ]; then
    echo "activate.sh: an OpenSSL outside /usr/local survived the sweep: $stray" >&2
    exit 1
fi

# Retire the blobs the caller named; leave this one and everything it did not
# name.  Every name here was accepted above, so this is only the deletion: a
# release the migration has already moved past is a retired toolchain, not a
# fallback, and leaving one on disk would let code that has not been migrated
# keep building against it.
for r in "$@"; do
    rm -f "/opt/ossl/$r.bin"
done

# What is installed now.  The archive's identity was settled by the digest
# above, so this is not a second identity check: it is the assertion that what
# came out of it is a working openssl reached through /usr/local, which a
# correctly-hashed archive of the wrong shape would still fail.
got=$(/usr/local/bin/openssl version | awk '{print $2}')
if [ -z "$got" ]; then
    echo "activate.sh: no working openssl under /usr/local after installing $ID" >&2
    exit 1
fi

# The FIPS module, made loadable.  A release that ships one ships a
# fipsmodule.cnf beside it holding the module's integrity MAC, and the packed
# prefix's copy does not match the module it ships with: build_ossl.sh strips
# ossl-modules/*.so after `make install_fips` has already hashed them, so the
# recorded MAC describes the unstripped file.  The FIPS provider refuses to
# initialise on that -- "SELF_TEST_post: missing config data" -- so the MAC is
# recomputed here, against the module that is actually installed.  Running
# fipsinstall as part of installing the prefix is also what a real deployment
# does with it.
#
# fips.cnf is the other half.  The provider is handed its MAC as *config data*,
# so OSSL_PROVIDER_load(ctx, "fips") on a library context with no configuration
# fails even when fipsmodule.cnf is correct; a context that wants the provider
# has to load a configuration that names the section.  This file activates the
# FIPS provider and nothing else, and no code reads it unless it asks for it by
# path -- openssl.cnf is left exactly as upstream ships it, with its own
# .include commented out, so the process-wide default context does not change.
#
# Both are skipped for the staged releases that have no FIPS module.
if [ -f /usr/local/lib/ossl-modules/fips.so ]; then
    if ! /usr/local/bin/openssl fipsinstall \
            -module /usr/local/lib/ossl-modules/fips.so \
            -out /usr/local/ssl/fipsmodule.cnf \
            -provider_name fips > /tmp/fipsinstall.log 2>&1; then
        echo "activate.sh: fipsinstall failed for $ID" >&2
        cat /tmp/fipsinstall.log >&2
        exit 1
    fi
    rm -f /tmp/fipsinstall.log
    cat > /usr/local/ssl/fips.cnf <<'CNF'
openssl_conf = openssl_init
.include /usr/local/ssl/fipsmodule.cnf
[openssl_init]
providers = provider_sect
[provider_sect]
fips = fips_sect
CNF
    chmod 0644 /usr/local/ssl/fipsmodule.cnf /usr/local/ssl/fips.cnf
fi

printf '%s\n' "$got" > /opt/ossl/VERSION
echo "OpenSSL $got installed under /usr/local (from $ID)"
