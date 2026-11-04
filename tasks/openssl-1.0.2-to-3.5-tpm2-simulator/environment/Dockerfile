# Agent environment for the TPM 2.0 simulator OpenSSL migration.
#
# Harbor builds one agent image for the whole task and keeps the container
# alive across steps, so this image carries the toolchain every step needs.
# What it must NOT carry is OpenSSL: each step targets a different release, and
# the step's setup.sh installs that one release under /usr/local.  OpenSSL
# 1.0.2 -- the version the repository vendors as a static archive -- is not
# staged here at all, so no step can quietly keep building against it.
#
# Debian's own libssl3 arrives as a dependency of git and curl.  libssl-dev is
# deliberately not installed here, so as this image is built libssl3 has no
# headers and no linker symlink and the only OpenSSL a compiler or `pkg-config`
# can find is the one under /usr/local; the ld.so.conf.d drop-in below keeps
# the loader agreeing at run time.
#
# The image is not where that ends, though.  Before the first step's setup.sh,
# Harbor installs the agent into the live container with
# `apt-get install -y curl bash nodejs npm procps`, and bookworm's npm pulls
# node-gyp, which pulls libssl-dev -- a second,
# complete OpenSSL development tree, arriving after every check this Dockerfile
# could make.  /opt/ossl/activate.sh removes it at the top of every step and
# asserts it is gone; see the sweep there.
FROM debian:bookworm-slim

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        bash ca-certificates coreutils curl diffutils file findutils \
        gawk git grep less nano procps ripgrep sed tar xz-utils \
        binutils build-essential cmake gdb make pkg-config patch perl \
        libasan8 libubsan1 \
        clang-14 clang-tidy-14 clang-tools-14 libclang-common-14-dev \
        libclang-rt-14-dev llvm-14 python3 python3-clang-14 \
    && rm -rf /var/lib/apt/lists/* \
# ca-certificates pulls in Debian's openssl command, which is a 3.0 build and
# would contradict whichever release the step has installed.  Leave exactly
# one openssl on the image so `openssl version` cannot mislead.
    && rm -f /usr/bin/openssl /usr/bin/c_rehash

# clang-14 is the only clang in the image; give it its unsuffixed names so the
# usual CC=clang / clang-tidy invocations work.
RUN for t in clang clang++ clang-tidy clang-check clang-apply-replacements \
             scan-build llvm-symbolizer llvm-cov llvm-profdata; do \
        if [ -x "/usr/bin/$t-14" ]; then ln -sf "/usr/bin/$t-14" "/usr/bin/$t"; fi; \
    done

# The loader must prefer /usr/local/lib over Debian's libssl3, whichever
# soname they happen to share.  Drop-ins are read in glob order, so 00- wins.
RUN printf '/usr/local/lib\n' > /etc/ld.so.conf.d/00-usr-local.conf && ldconfig

# The releases each step's setup.sh installs, named for their contents rather
# than for their versions: this is one container for all ten steps, so step k+1's
# archive is on disk while step k is being worked on, and version-named files
# here would list the ladder to anyone who ran ls.  Each step's setup.sh names
# the blob it installs and the blobs it retires -- a retired release is not left
# on disk while the step that retired it is being worked on -- and activate.sh
# checks every name against the file's SHA-256.  The mapping from version to
# blob is in build/ossl/blobs.txt, on the authoring side.
#
# The names are hashed and the contents are wrapped, because a hashed name only
# hides the ladder from `ls`: an xz archive of a DESTDIR still answers
# `tar -xJOf ... opensslv.h` with the release it holds.  Each blob is XORed
# under a key of its own (osslwrap.py) and each step's setup.sh carries exactly
# one key, so the image holds every rung and the step can open the one it is
# on; the other six stay wrapped.
#
# Only the wrapped blobs and the two scripts that open them are copied.  The
# build context's ossl/ also holds the same seven releases unwrapped -- as
# blob-*.tar.xz and as version-named openssl-<v>.tar.xz -- and copying the whole
# directory would put every one of them in /opt/ossl in plain sight: the ladder
# readable by `ls`, and every retired release left on disk, because activate.sh
# retires a release by deleting its .bin and nothing else.
COPY ossl/activate.sh ossl/osslwrap.py ossl/*.bin /opt/ossl/
# An explicit mode, not `chmod +x`: COPY preserves the build context's bits, and
# +x on a 0600 file yields 0700, which is only executable for root.  The glob
# has to match something -- a build where stage_ossl.sh has not run fails here
# rather than shipping an image with no ladder in it.
RUN chmod 0755 /opt/ossl/activate.sh /opt/ossl/osslwrap.py \
    && chmod 0644 /opt/ossl/*.bin

# The repository, at the pinned commit, unmodified.
COPY app /app

# Keep the countdown and every [steps.agent].timeout_sec in task.toml
# synchronized.  Harbor enforces the real timeout; this only reports it.
COPY timer.sh /app/timer.sh
RUN chmod +x /app/timer.sh

WORKDIR /app
