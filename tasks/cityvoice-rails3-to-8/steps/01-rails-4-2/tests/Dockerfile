# Step 1 verifier -- Ruby 2.3.8 / Rails 4.2.
#
# The build context is this `tests/` directory and nothing else: Harbor builds
# this file with `steps/01-rails-4-2/tests/` as the context, so it cannot reach
# the task root, `environment/`, or another step.  Everything the scorer needs
# is therefore duplicated into each step -- the interpreter recipe, the
# harness, the gates, the frozen golden, the vendored libfaketime.
# Identical copies of those files live in every step's `tests/` and must be
# kept in sync: a stale harness does not fail, it reports a number.
#
# One Ruby, deliberately.  The image carries only the interpreter this step
# targets, mounted under rbenv the same way the agent image mounts its eight.
# rbenv reads the tree's own `.ruby-version`, so a candidate that migrated the
# code but not the version file selects an interpreter that is not installed
# and the scorer sees that as a failed step rather than as a passing build.
# That makes the interpreter an artifact-level gate rather than a claim.

# ---- toolbase ---------------------------------------------------------------
# Every stage below needs a compiler, so apt runs once and is shared.
FROM debian:bookworm-slim AS toolbase
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
      build-essential ca-certificates curl wget git pkg-config \
      libreadline-dev zlib1g-dev libyaml-dev libffi-dev libgdbm-dev \
      libncurses-dev libxml2-dev libxslt1-dev libpq-dev bison autoconf \
      util-linux locales tzdata \
 && rm -rf /var/lib/apt/lists/*

# ---- ssl --------------------------------------------------------------------
# Ruby 2.3's openssl extension predates OpenSSL 1.1.  Steps on 2.4-3.0 build
# 1.1.1w here instead; steps on 3.1+ drop this stage and use bookworm's own.
FROM toolbase AS ssl
RUN cd /tmp && curl -sSfL --retry 5 -O https://www.openssl.org/source/old/1.0.2/openssl-1.0.2u.tar.gz \
 && tar xzf openssl-1.0.2u.tar.gz && cd openssl-1.0.2u \
 && ./config --prefix=/opt/openssl-1.0.2 --openssldir=/opt/openssl-1.0.2/ssl shared zlib >/dev/null \
 && make -j"$(nproc)" >/dev/null 2>&1 && make install_sw >/dev/null \
 && cp /etc/ssl/certs/ca-certificates.crt /opt/openssl-1.0.2/ssl/cert.pem \
 && rm -rf /tmp/openssl-1.0.2u*

# ---- ruby -------------------------------------------------------------------
FROM ssl AS ruby
COPY build-ruby.sh /usr/local/bin/build-ruby.sh
RUN chmod 0755 /usr/local/bin/build-ruby.sh && build-ruby.sh 2.3.8 /opt/openssl-1.0.2 1.17.3

# ---- faketime ---------------------------------------------------------------
# The same pinned release the recorder used, built from the same vendored
# tarball, so the scorer and the golden cannot differ by a distribution patch.
FROM toolbase AS faketime
COPY libfaketime.tar.gz /tmp/
RUN cd /tmp && tar xzf libfaketime.tar.gz && cd libfaketime-0.9.10 \
 && make -C src FAKETIME_COMPILE_CFLAGS="-DFAKE_STAT -DFAKE_SLEEP" >/dev/null \
 && install -D -m 0644 src/libfaketime.so.1 /usr/local/lib/faketime/libfaketime.so.1

# ---- scorer -----------------------------------------------------------------
FROM toolbase AS scorer

# PostgreSQL 16 from PGDG, the same server the agent image runs, so a schema
# the candidate loaded in one is loadable in the other.
RUN install -d /usr/share/postgresql-common/pgdg \
 && curl -sSfL --retry 5 -o /usr/share/postgresql-common/pgdg/apt.postgresql.org.asc \
      https://www.postgresql.org/media/keys/ACCC4CF8.asc \
 && echo "deb [signed-by=/usr/share/postgresql-common/pgdg/apt.postgresql.org.asc] https://apt.postgresql.org/pub/repos/apt bookworm-pgdg main" \
      > /etc/apt/sources.list.d/pgdg.list \
 && apt-get update \
 && apt-get install -y --no-install-recommends \
      postgresql-16 postgresql-client-16 \
      nodejs npm python3 python3-venv \
 && rm -rf /var/lib/apt/lists/*

RUN sed -i 's/^# *en_US.UTF-8/en_US.UTF-8/' /etc/locale.gen && locale-gen
ENV LANG=en_US.UTF-8 LC_ALL=en_US.UTF-8 TZ=UTC

RUN printf 'local all all trust\nhost all all 127.0.0.1/32 trust\nhost all all ::1/128 trust\n' \
      > /etc/postgresql/16/main/pg_hba.conf \
 && printf 'fsync = off\nfull_page_writes = off\nsynchronous_commit = off\n' \
      >> /etc/postgresql/16/main/postgresql.conf

COPY --from=ssl /opt/openssl-1.0.2 /opt/openssl-1.0.2
RUN printf '/opt/openssl-1.0.2/lib\n' > /etc/ld.so.conf.d/cityvoice-openssl.conf && ldconfig
COPY --from=faketime /usr/local/lib/faketime /usr/local/lib/faketime

ENV RBENV_ROOT=/usr/local/rbenv
COPY --from=ruby /opt/rubies/2.3.8 /opt/rubies/2.3.8
# `2.3.8` and `ruby-2.3.8` both resolve.  rbenv matches the contents of
# `.ruby-version` literally, and both spellings are in circulation -- rbenv
# writes the bare number, chruby and rvm write the prefixed one.  Accepting
# both costs a symlink and keeps the gate on *which interpreter runs* rather
# than on how the candidate's tool spelled it.
RUN git clone --quiet --depth 1 https://github.com/rbenv/rbenv.git "$RBENV_ROOT" \
 && mkdir -p "$RBENV_ROOT/versions" \
 && ln -s /opt/rubies/2.3.8 "$RBENV_ROOT/versions/2.3.8" \
 && ln -s /opt/rubies/2.3.8 "$RBENV_ROOT/versions/ruby-2.3.8" \
 && echo 2.3.8 > "$RBENV_ROOT/version"
ENV PATH=/usr/local/rbenv/shims:/usr/local/rbenv/bin:/usr/lib/postgresql/16/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
RUN rbenv rehash && ruby -v && gem -v && bundle -v

# Third-party dependency caches are warm.  The gems this
# step's own solution resolves to are installed here, at image build time, into
# a bundle path outside `/app` -- so scoring installs from a local cache and the
# clock measures the candidate's code, not rubygems.org.  The network stays
# open at scoring time, so a candidate whose lockfile differs still
# resolves; it just pays for the difference.
#
# BUNDLE_APP_CONFIG points outside /app for two reasons.  Bundler 1.x writes
# `.bundle/config` next to the Gemfile, which would edit the artifact under the
# scorer; and the candidate's own `/app/.bundle/config` arrives in the
# artifact, so without this the scorer would silently adopt whatever bundle
# path, frozen flag or `without` group the candidate happened to leave behind.
#
# RUBYOPT=-W0 is not a convenience: the baseline was recorded with it,
# interpreter warnings are written to the same stdout the rake cases are
# scored on, and Ruby's warning set is not stable across the
# eight interpreters these thirteen steps use.
ENV BUNDLE_PATH=/bundle \
    BUNDLE_APP_CONFIG=/bundle/.bundleconfig \
    BUNDLE_JOBS=7 \
    BUNDLE_RETRY=3 \
    RUBYOPT=-W0
COPY warm/Gemfile warm/Gemfile.lock /tmp/warm/
RUN cd /tmp/warm && bundle install && rm -rf /tmp/warm

COPY . /tests

# Modes, and who may read what.
#
# `COPY` preserves the build context's modes, and this tree is checked out
# under a umask that leaves files 0600 and directories 0700.  That is the right
# default here -- everything under /tests belongs to the verifier, and the
# candidate's code runs in this container as uid 65534 -- but the probes
# have to be an exception: phase 4 runs `bootprobe.rb`, `depdrive.rb` and `envboot.rb`
# unprivileged, because they boot the candidate's application, and a 0700
# parent makes the interpreter fail to *open the probe* with
# `Permission denied -- /tests/cvgate/bootprobe.rb (LoadError)`.  That is a
# probe that could not be asked, which costs its gates and looks exactly like a
# candidate whose application does not boot.
#
# So `cvgate/` is opened for reading and nothing else is.  Root owns all of it,
# so readable is not writable: the candidate can read the checks -- they are
# the step's stated requirements anyway -- and cannot change
# them.  `golden/`, `overlays/`, `step/` and `cvh/` stay 0700, because those
# hold the recorded answers, and an application that could read
# `golden/baseline.json` while the suite was running could answer from it.
RUN chmod 0755 /tests/test.sh \
 && chmod 0755 /tests/cvgate \
 && chmod 0644 /tests/cvgate/*.rb \
 && for private in golden overlays step cvh warm; do \
        chmod 0700 "/tests/$private"; \
    done
WORKDIR /app
