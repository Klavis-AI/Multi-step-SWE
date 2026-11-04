# Agent environment for the CityVoice Rails 3 -> 8 migration ladder.
#
# Harbor gives a multi-step task exactly one agent image (StepConfig carries a
# per-step verifier environment but no per-step agent environment), so this
# image holds every interpreter the ladder lands on.  Each step's VERIFIER
# image holds exactly one of them, which is what turns the tree's own
# .ruby-version into an artifact-level gate rather than a convenience.
#
# Ruby is selected the way the repository already selects it: rbenv reads the
# tree's own .ruby-version.

# ---------------------------------------------------------------- toolchain
FROM debian:bookworm-slim AS toolbase

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
      autoconf bison build-essential ca-certificates curl libffi-dev libgdbm-dev \
      libgmp-dev libncurses-dev libreadline-dev libssl-dev libxml2-dev \
      libxslt1-dev libyaml-dev pkg-config zlib1g-dev \
 && rm -rf /var/lib/apt/lists/*

# Ruby 2.1 and 2.3 predate OpenSSL 1.1; Ruby 2.4 through 3.0 predate OpenSSL 3.
RUN cd /tmp && curl -sSfL --retry 5 -O https://www.openssl.org/source/old/1.0.2/openssl-1.0.2u.tar.gz \
 && tar xzf openssl-1.0.2u.tar.gz && cd openssl-1.0.2u \
 && ./config --prefix=/opt/openssl-1.0.2 --openssldir=/opt/openssl-1.0.2/ssl shared zlib >/dev/null \
 && make -j"$(nproc)" >/dev/null 2>&1 && make install_sw >/dev/null \
 && mkdir -p /opt/openssl-1.0.2/ssl \
 && cp /etc/ssl/certs/ca-certificates.crt /opt/openssl-1.0.2/ssl/cert.pem \
 && rm -rf /tmp/openssl-1.0.2u*

RUN cd /tmp && curl -sSfL --retry 5 -O https://github.com/openssl/openssl/releases/download/OpenSSL_1_1_1w/openssl-1.1.1w.tar.gz \
 && tar xzf openssl-1.1.1w.tar.gz && cd openssl-1.1.1w \
 && ./config --prefix=/opt/openssl-1.1.1 --openssldir=/opt/openssl-1.1.1/ssl shared zlib >/dev/null \
 && make -j"$(nproc)" >/dev/null 2>&1 && make install_sw >/dev/null \
 && mkdir -p /opt/openssl-1.1.1/ssl \
 && cp /etc/ssl/certs/ca-certificates.crt /opt/openssl-1.1.1/ssl/cert.pem \
 && rm -rf /tmp/openssl-1.1.1w*

RUN printf '/opt/openssl-1.0.2/lib\n/opt/openssl-1.1.1/lib\n' > /etc/ld.so.conf.d/cityvoice-openssl.conf \
 && ldconfig

COPY build-ruby.sh /usr/local/bin/build-ruby.sh
RUN chmod +x /usr/local/bin/build-ruby.sh

# ------------------------------------------------------------------- rubies
# One stage per interpreter: BuildKit overlaps them and no build residue
# reaches the final image.  The third argument is the newest Bundler that
# interpreter's own RubyGems can resolve and install.
FROM toolbase AS ruby-2-1-7
RUN build-ruby.sh 2.1.7  /opt/openssl-1.0.2 1.17.3
FROM toolbase AS ruby-2-3-8
RUN build-ruby.sh 2.3.8  /opt/openssl-1.0.2 1.17.3
FROM toolbase AS ruby-2-4-10
RUN build-ruby.sh 2.4.10 /opt/openssl-1.1.1 1.17.3
FROM toolbase AS ruby-2-7-8
RUN build-ruby.sh 2.7.8  /opt/openssl-1.1.1 2.4.22
FROM toolbase AS ruby-3-0-7
RUN build-ruby.sh 3.0.7  /opt/openssl-1.1.1 2.4.22
FROM toolbase AS ruby-3-1-7
RUN build-ruby.sh 3.1.7  system 2.5.23
FROM toolbase AS ruby-3-2-11
RUN build-ruby.sh 3.2.11 system 2.6.9
FROM toolbase AS ruby-3-4-10
RUN build-ruby.sh 3.4.10 system 2.6.9

# -------------------------------------------------------------------- final
FROM debian:bookworm-slim

ENV DEBIAN_FRONTEND=noninteractive

# The build toolchain stays installed: every step compiles native gems (pg,
# nokogiri, ffi, bcrypt) against the interpreter the tree names.
RUN apt-get update && apt-get install -y --no-install-recommends \
      autoconf bash bison build-essential ca-certificates curl file git gnupg \
      jq less libffi-dev libgdbm-dev libgmp-dev libncurses-dev libreadline-dev \
      libsqlite3-dev libssl-dev libxml2-dev libxslt1-dev libyaml-dev locales \
      nodejs npm patch pkg-config procps ripgrep tzdata unzip vim-tiny zlib1g-dev \
 && rm -rf /var/lib/apt/lists/*

# PostgreSQL 16 from the project's own apt repository.
RUN install -d /usr/share/postgresql-common/pgdg \
 && curl -sSfL --retry 5 -o /usr/share/postgresql-common/pgdg/apt.postgresql.org.asc \
      https://www.postgresql.org/media/keys/ACCC4CF8.asc \
 && echo "deb [signed-by=/usr/share/postgresql-common/pgdg/apt.postgresql.org.asc] https://apt.postgresql.org/pub/repos/apt bookworm-pgdg main" \
      > /etc/apt/sources.list.d/pgdg.list \
 && apt-get update \
 && apt-get install -y --no-install-recommends postgresql-16 postgresql-client-16 libpq-dev \
 && rm -rf /var/lib/apt/lists/*

RUN sed -i 's/^# *en_US.UTF-8/en_US.UTF-8/' /etc/locale.gen && locale-gen
ENV LANG=en_US.UTF-8 LC_ALL=en_US.UTF-8 TZ=UTC

# config/database.yml.example connects as the `postgres` role over the local
# socket with no password, so the cluster trusts local connections.
RUN printf 'local all all trust\nhost all all 127.0.0.1/32 trust\nhost all all ::1/128 trust\n' \
      > /etc/postgresql/16/main/pg_hba.conf \
 && printf 'fsync = off\nfull_page_writes = off\nsynchronous_commit = off\n' \
      >> /etc/postgresql/16/main/postgresql.conf

RUN npm install -g --silent yarn@1.22.22 && npm cache clean --force

COPY --from=toolbase /opt/openssl-1.0.2 /opt/openssl-1.0.2
COPY --from=toolbase /opt/openssl-1.1.1 /opt/openssl-1.1.1
RUN printf '/opt/openssl-1.0.2/lib\n/opt/openssl-1.1.1/lib\n' > /etc/ld.so.conf.d/cityvoice-openssl.conf \
 && ldconfig

ENV RBENV_ROOT=/usr/local/rbenv
RUN git clone --quiet --depth 1 https://github.com/rbenv/rbenv.git "$RBENV_ROOT" \
 && mkdir -p "$RBENV_ROOT/versions"

# Each interpreter keeps the prefix it was configured with.  rbenv reaches it
# through a symlink rather than owning the directory, because `gem install`
# bakes an absolute `#!/opt/rubies/<v>/bin/ruby` shebang into every binstub:
# relocating the tree would leave `bundle` pointing at an interpreter that is
# no longer there.  rbconfig.rb recomputes its own prefix from __FILE__, so
# everything else in the installation follows the symlink correctly.
COPY --from=ruby-2-1-7  /opt/rubies/2.1.7  /opt/rubies/2.1.7
COPY --from=ruby-2-3-8  /opt/rubies/2.3.8  /opt/rubies/2.3.8
COPY --from=ruby-2-4-10 /opt/rubies/2.4.10 /opt/rubies/2.4.10
COPY --from=ruby-2-7-8  /opt/rubies/2.7.8  /opt/rubies/2.7.8
COPY --from=ruby-3-0-7  /opt/rubies/3.0.7  /opt/rubies/3.0.7
COPY --from=ruby-3-1-7  /opt/rubies/3.1.7  /opt/rubies/3.1.7
COPY --from=ruby-3-2-11 /opt/rubies/3.2.11 /opt/rubies/3.2.11
COPY --from=ruby-3-4-10 /opt/rubies/3.4.10 /opt/rubies/3.4.10

ENV PATH=/usr/local/rbenv/shims:/usr/local/rbenv/bin:/usr/lib/postgresql/16/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
RUN for v in 2.1.7 2.3.8 2.4.10 2.7.8 3.0.7 3.1.7 3.2.11 3.4.10; do \
      ln -s "/opt/rubies/$v" "$RBENV_ROOT/versions/$v"; \
    done \
 && echo 3.4.10 > "$RBENV_ROOT/version" && rbenv rehash \
 && for v in 2.1.7 2.3.8 2.4.10 2.7.8 3.0.7 3.1.7 3.2.11 3.4.10; do \
      RBENV_VERSION="$v" ruby -v && RBENV_VERSION="$v" gem -v \
        && RBENV_VERSION="$v" bundle -v || exit 1; \
    done

# ---- the advisory database, and the tool that reads it ----------------------
# Step 13 asks the candidate to get the resolved dependency graph clean of
# known advisories, measured against a fixed snapshot of the advisory database
# rather than whatever upstream holds on the day the step runs.
#
# The snapshot is rubysec/ruby-advisory-db at commit
# 44784c295391577f25d198a9205eae4ba73ec4da (2026-09-16), the same bytes the
# step-13 verifier scores against (a copy of this tarball sits in
# steps/13-hardening/tests/extra/).  `COMMIT` is checked rather than trusted so
# that a tarball rebuilt from a different upstream state fails this build
# instead of silently moving what the candidate is told.
#
# It is here from step 01, not from step 13, because Harbor gives a task one
# environment image and every step runs in it -- the same reason all eight
# interpreters are present from the first rung.  Nothing below step 13 reads
# it.
COPY ruby-advisory-db.tar.gz /tmp/
RUN mkdir -p /opt/ruby-advisory-db \
 && tar xzf /tmp/ruby-advisory-db.tar.gz -C /opt/ruby-advisory-db \
 && rm -f /tmp/ruby-advisory-db.tar.gz \
 && chmod -R a+rX /opt/ruby-advisory-db \
 && test "$(cat /opt/ruby-advisory-db/COMMIT)" = 44784c295391577f25d198a9205eae4ba73ec4da

# `bundle-audit` 0.9.3, in a GEM_HOME of its own.  Installing it into any
# interpreter's own gem home would put `thor` on the candidate's load path,
# where `bundler/setup` would either complain about it or, worse, let the
# application require it; and installing it into the application's bundle would
# put it in the lockfile the verifier reads, which is the artifact under test.
#
# The wrapper pins both the interpreter and the gem home, so the tool works
# from any directory and at any `.ruby-version` the candidate has set -- at
# step 03 the tree still says 2.1.7, and a 2.1.7 running thor 1.5.0 is a
# NoMethodError, not an audit.
#
# BUNDLER_AUDIT_DB points at the snapshot, so `bundle-audit check` with no
# arguments reads the same database the scorer does.  The instruction uses
# `--no-update`, which keeps it on that snapshot.
RUN env GEM_HOME=/opt/bundler-audit GEM_PATH=/opt/bundler-audit RBENV_VERSION=3.4.10 \
      gem install --no-document bundler-audit -v 0.9.3 \
 && printf '%s\n' \
      '#!/bin/sh' \
      '# bundler-audit 0.9.3, pinned to 3.4.10 and to the snapshot in /opt.' \
      'exec env RBENV_VERSION=3.4.10 GEM_HOME=/opt/bundler-audit GEM_PATH=/opt/bundler-audit \' \
      '    BUNDLER_AUDIT_DB="${BUNDLER_AUDIT_DB:-/opt/ruby-advisory-db}" \' \
      '    /opt/bundler-audit/bin/bundle-audit "$@"' \
      > /usr/local/bin/bundle-audit \
 && chmod 0755 /usr/local/bin/bundle-audit \
 && chmod -R a+rX /opt/bundler-audit \
 && bundle-audit version
ENV BUNDLER_AUDIT_DB=/opt/ruby-advisory-db

COPY entrypoint.sh /usr/local/bin/entrypoint.sh
COPY timer.sh /usr/local/bin/timer.sh
RUN chmod 0755 /usr/local/bin/entrypoint.sh /usr/local/bin/timer.sh

# The repository at its pinned commit, unmodified.
COPY repo /app
WORKDIR /app

ENTRYPOINT ["/usr/local/bin/entrypoint.sh"]
