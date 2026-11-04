# Verifier image for step07, built with steps/07-params-not-ctrls/tests/ as the Docker
# build context: it cannot reach the task root or another step, so every step
# carries its own copy of the shared machinery under lib/.  Harbor runs
# /tests/test.sh from this image against a fresh copy of the candidate's /app.
#
# The image installs OpenSSL 3.2.5 and only OpenSSL 3.2.5.  That is the whole
# reason the verifier is a separate image per step: a tree that still builds
# against the previous release cannot be made to look migrated by a verifier
# that happens to have both installed.
FROM debian:bookworm-slim

ENV DEBIAN_FRONTEND=noninteractive

# Grading's toolchain, independent of the agent image: the verifier must not
# depend on anything the candidate could have modified.  clang/LLVM 14 is here
# for the AST gate (libclang), binutils for the link gate, util-linux for
# setpriv, and cmake/make/gcc to rebuild the candidate's tree from source.
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        bash binutils build-essential ca-certificates cmake coreutils \
        diffutils file findutils gawk grep make perl pkg-config procps \
        python3 sed tar util-linux xz-utils \
        libclang-common-14-dev llvm-14 python3-clang-14 \
    && rm -rf /var/lib/apt/lists/* \
# Debian's openssl command is a 3.0 build and would contradict the release this
# image installs.  Leave exactly one openssl on the image.
    && rm -f /usr/bin/openssl /usr/bin/c_rehash

# The loader must prefer /usr/local/lib over Debian's libssl3 when they share a
# soname.  Drop-ins are read in glob order, so 00- wins.
RUN printf '/usr/local/lib\n' > /etc/ld.so.conf.d/00-usr-local.conf && ldconfig

# The release, staged as a prefix packed from DESTDIR with --prefix=/usr/local,
# so untarring at "/" makes it the system OpenSSL with no environment variables
# in play.  Asserted immediately: a verifier that silently installed the wrong
# release would score every candidate against the wrong standard.
COPY ossl/openssl-3.2.5.tar.xz /tmp/ossl.tar.xz
RUN tar -xJf /tmp/ossl.tar.xz -C / \
    && rm -f /tmp/ossl.tar.xz \
    && ldconfig \
    && got=$(/usr/local/bin/openssl version | awk '{print $2}') \
    && [ "$got" = "3.2.5" ] || { echo "staged archive reports $got, not 3.2.5" >&2; exit 1; }

# The FIPS module, made loadable, exactly as environment/ossl/activate.sh does
# it for the agent's container -- the two have to agree, because the context
# the candidate builds against and the context this image's probe measures are
# supposed to be one environment.  The packed prefix's fipsmodule.cnf records
# the MAC of the module before build_ossl.sh stripped it, and a library context
# with no configuration is not handed the MAC at all, so a release that ships a
# FIPS module needs both halves repaired here.  1.1.0l and 1.1.1w ship no
# module and skip it.
RUN if [ -f /usr/local/lib/ossl-modules/fips.so ]; then \
        /usr/local/bin/openssl fipsinstall \
            -module /usr/local/lib/ossl-modules/fips.so \
            -out /usr/local/ssl/fipsmodule.cnf \
            -provider_name fips \
        && printf '%s\n' \
            'openssl_conf = openssl_init' \
            '.include /usr/local/ssl/fipsmodule.cnf' \
            '[openssl_init]' \
            'providers = provider_sect' \
            '[provider_sect]' \
            'fips = fips_sect' > /usr/local/ssl/fips.cnf \
        && chmod 0644 /usr/local/ssl/fipsmodule.cnf /usr/local/ssl/fips.cnf; \
    fi

# /app is repopulated from the collected artifact and /logs/verifier is bind-
# mounted at run time; creating them keeps workdir valid at container start.
RUN mkdir -p /app /logs/verifier

# Which step this image grades.  Baked into the image, not read from the
# mounted tree, so the stamp test.sh writes names the image that actually ran.
ENV STEP_ID=step07
ENV OSSL_VERSION=3.2.5

# /tests is root-only.  Candidate code runs inside this container --
# its Makefile, its library, its simulator -- and the recordings are the
# answers.  Whatever the unprivileged side genuinely needs is copied out to a
# world-readable staging directory by stage_lib(); the expectations are not
# among them and stay unreadable to anything but root.
COPY . /tests
RUN chmod 0700 /tests \
    && chmod -R go-rwx /tests \
    && chmod u+x /tests/test.sh /tests/lib/*.sh /tests/lib/*.py

WORKDIR /app
