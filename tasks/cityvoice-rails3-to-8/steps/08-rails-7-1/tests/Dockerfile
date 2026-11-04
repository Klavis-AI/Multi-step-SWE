# Step 8 verifier -- Ruby 3.2.11 / Rails 7.1.
#
# Structurally this is step 7's file: no OpenSSL stage, bookworm's own
# `libssl-dev`, one interpreter built from the same `build-ruby.sh`.  Two
# numbers move.
#
# The interpreter is 3.2.11 because this step moves both pins at once.  Ruby
# 3.2 removed `File.exists?`, which this tree calls from config/boot.rb and
# spec/support/save_fixture.rb and which sass 3.2.19 calls from its own
# version.rb -- so the interpreter cannot move without the bundle moving, and
# Rails 7.1 is the release that sits with it.  Steps 5 and 6 moved two pins for
# the same reason: neither one can move alone.
#
# The Bundler moves with it: 2.6.9, because 2.6 is the first series that
# requires Ruby 3.1 or newer, and 3.2.11 is the interpreter the agent image
# pairs it with (environment/Dockerfile).  Holding
# 2.5.23 here would leave the candidate resolving with one Bundler and this
# image installing with another, which is the one variable a verifier should
# never introduce by itself.
#
# The images are still built separately and tagged by their own payload hash,
# because what also differs between them is the `tests/` tree copied in at the
# end -- the golden, the overlays, the profile and the warm bundle -- and that
# is the part a stale copy would silently score.
#
# The build context is this `tests/` directory and nothing else: Harbor builds
# this file with `steps/08-rails-7-1/tests/` as the context, so it cannot reach
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
# `libssl-dev` is bookworm's own, as at step 7: 3.2, like 3.1, builds and runs
# against OpenSSL 3, so there is no ssl stage below.
FROM debian:bookworm-slim AS toolbase
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
      build-essential ca-certificates curl wget git pkg-config \
      libreadline-dev zlib1g-dev libyaml-dev libffi-dev libgdbm-dev \
      libncurses-dev libssl-dev libxml2-dev libxslt1-dev libpq-dev bison autoconf \
      util-linux locales tzdata \
 && rm -rf /var/lib/apt/lists/*

# ---- ruby -------------------------------------------------------------------
# `system` where steps 1-6 name a prefix.  The recipe is otherwise the one the
# agent image uses for the same interpreter, from the same script.
FROM toolbase AS ruby
COPY build-ruby.sh /usr/local/bin/build-ruby.sh
RUN chmod 0755 /usr/local/bin/build-ruby.sh && build-ruby.sh 3.2.11 system 2.6.9

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

COPY --from=faketime /usr/local/lib/faketime /usr/local/lib/faketime

ENV RBENV_ROOT=/usr/local/rbenv
COPY --from=ruby /opt/rubies/3.2.11 /opt/rubies/3.2.11
# `3.2.11` and `ruby-3.2.11` both resolve.  rbenv matches the contents of
# `.ruby-version` literally, and both spellings are in circulation -- rbenv
# writes the bare number, chruby and rvm write the prefixed one.  Accepting
# both costs a symlink and keeps the gate on *which interpreter runs* rather
# than on how the candidate's tool spelled it.
RUN git clone --quiet --depth 1 https://github.com/rbenv/rbenv.git "$RBENV_ROOT" \
 && mkdir -p "$RBENV_ROOT/versions" \
 && ln -s /opt/rubies/3.2.11 "$RBENV_ROOT/versions/3.2.11" \
 && ln -s /opt/rubies/3.2.11 "$RBENV_ROOT/versions/ruby-3.2.11" \
 && echo 3.2.11 > "$RBENV_ROOT/version"
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
# eight interpreters these thirteen steps use.  3.2 is where the bundle starts
# emitting `undefining the allocator of T_DATA class BigDecimal` from
# bigdecimal 1.4.4, and `-W0` does NOT silence it -- that is a runtime warning,
# not a verbose-mode one -- so it stays visible in `bootprobe.warnings`, which
# is reported and not gated.  `-W0` itself stays,
# because the baseline was recorded with it and removing it here would make the
# scorer's stdout differ from the recorder's for reasons no step asked for.
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
