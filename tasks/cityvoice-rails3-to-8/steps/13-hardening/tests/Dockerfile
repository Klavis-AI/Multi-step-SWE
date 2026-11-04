# Step 13 verifier -- Ruby 3.4.10 / Rails 8.1, hardened.
#
# The interpreter does not move here.  Step 12 was the last rung that changed
# it, and this file's `ruby` stage is step 12's, argument for argument:
# 3.4.10, `system` OpenSSL, Bundler 2.6.9.  Diffing this against step 12's
# verifier Dockerfile should show three things and nothing else --
# this header, the `advisories` block below, and `extra` in the closing chmod
# list.  Anything else in that diff is drift, because the last rung of a
# ladder that has already arrived at its target version has no business
# rebuilding the target.
#
# The warm bundle is this step's own, as every rung's is, and here the delta
# is the step: the same 153 specs step 12 resolves, with three of them moved
# forward -- `redcarpet 3.2.3 -> 3.6.1`, `uglifier 2.7.2 -> 4.2.1` and
# `jwt 1.5.6 -> 3.3.0`, the three the pinned advisory database has something
# to say about that the candidate can actually move.  Warming them decides
# what the clock measures, not what the gates read.
#
# ---- what this image can answer that step 12's could not -------------------
#
# One thing: `/opt/ruby-advisory-db`, a snapshot of rubysec/ruby-advisory-db
# at commit 44784c295391577f25d198a9205eae4ba73ec4da (2026-09-16), unpacked
# below.  `cvgate/audit.rb` resolves the candidate's lockfile against it.
#
# The snapshot is the point.  The upstream database gains advisories most
# weeks -- 1245 of them over 470 gems at this commit -- and a gate whose
# expected value changes when a stranger merges a pull request is not a gate,
# it is a clock.  Pinning it means "no known advisory" is a statement about a
# fixed set of facts that the instruction can name and the candidate can
# check, rather than about whatever github served during the run.  It also
# means this image builds the same way with the network unplugged, which
# `bundle-audit --update` would not.
#
# `audit.rb` reads that directory itself rather than shelling out to the
# `bundle-audit` CLI, and the reason is measured rather than stylistic.  The
# CLI exits 1 both for "found advisories" and for "no database here", it
# honours a `.bundler-audit.yml` sitting in the candidate's tree -- which is
# an ignore list the candidate writes, scoring themselves -- and it reports
# its findings as prose meant for a terminal.  Reading the YAML directly
# costs about eighty lines and removes all three.  The candidate still gets
# the CLI in the agent image, pointed at the same snapshot, because the
# instruction has to be answerable with an instrument and not by guessing;
# the verifier simply does not use theirs.  This reader reproduces
# bundler-audit 0.9.3's own numbers on both trees exactly: 31 advisories over
# 7 gems on the arriving tree, 0 on the solved one.
#
# A step whose image does NOT carry the database -- every rung below this one
# -- runs the same probe and gets a report that says `present: false` and
# carries no gem list at all.  `score.py` reads a missing path as a failed
# check, so the failure mode of a verifier built without the snapshot is that
# this step's advisory gates score zero, not that they pass unexamined.
#
# The images are still built separately and tagged by their own payload hash,
# because what differs between them is the `tests/` tree copied in at the end
# -- the golden, the overlays, the profile and the warm bundle -- and that is
# the part a stale copy would silently score.
# ---- toolbase ---------------------------------------------------------------
# Every stage below needs a compiler, so apt runs once and is shared.
# `libssl-dev` is bookworm's own, as it has been since step 7: 3.4, like 3.2
# and 3.1 before it, builds and runs against OpenSSL 3, so there is no ssl
# stage below and no vendored 1.1.1 to keep alive.
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
RUN chmod 0755 /usr/local/bin/build-ruby.sh && build-ruby.sh 3.4.10 system 2.6.9

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
COPY --from=ruby /opt/rubies/3.4.10 /opt/rubies/3.4.10
# `3.4.10` and `ruby-3.4.10` both resolve.  rbenv matches the contents of
# `.ruby-version` literally, and both spellings are in circulation -- rbenv
# writes the bare number, chruby and rvm write the prefixed one.  Accepting
# both costs a symlink and keeps the gate on *which interpreter runs* rather
# than on how the candidate's tool spelled it.
RUN git clone --quiet --depth 1 https://github.com/rbenv/rbenv.git "$RBENV_ROOT" \
 && mkdir -p "$RBENV_ROOT/versions" \
 && ln -s /opt/rubies/3.4.10 "$RBENV_ROOT/versions/3.4.10" \
 && ln -s /opt/rubies/3.4.10 "$RBENV_ROOT/versions/ruby-3.4.10" \
 && echo 3.4.10 > "$RBENV_ROOT/version"
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
#
# Step 12 is where that mattered.  Three warnings landed on the tree arriving
# there -- the `bigdecimal` and `redcarpet` allocator lines and the `ostruct`
# bundled-gem notice -- and step 12 gated all three away.  The tree arriving
# HERE boots silent: `bootprobe.warnings` and `bootprobe.bundled_gem_warnings`
# are both `[]` on step 12's solved tree.  That is a floor this rung
# inherits, so this step's profile guards it and scores no point for it,
# and `cvgate/bootprobe.rb#capture_warnings` still restores `$VERBOSE = false`
# for the length of its own boot so that a candidate who reintroduces a
# bundled-gem require is still seen through `-W0`.
ENV BUNDLE_PATH=/bundle \
    BUNDLE_APP_CONFIG=/bundle/.bundleconfig \
    BUNDLE_JOBS=7 \
    BUNDLE_RETRY=3 \
    RUBYOPT=-W0
COPY warm/Gemfile warm/Gemfile.lock /tmp/warm/
RUN cd /tmp/warm && bundle install && rm -rf /tmp/warm

# ---- the pinned advisory database ------------------------------------------
# Unpacked, not cloned: no network at build time, no moving target at scoring
# time.  `COMMIT` is checked here rather than trusted, so that a tarball that
# was rebuilt from a different upstream state fails this build instead of
# quietly moving every expected count in this step's profile.  `audit.rb`
# reports the same string in `audit.db.commit`, which puts the pin in the
# evidence file of every run.
COPY extra/ruby-advisory-db.tar.gz /tmp/
RUN mkdir -p /opt/ruby-advisory-db \
 && tar xzf /tmp/ruby-advisory-db.tar.gz -C /opt/ruby-advisory-db \
 && rm -f /tmp/ruby-advisory-db.tar.gz \
 && chmod -R a+rX /opt/ruby-advisory-db \
 && test "$(cat /opt/ruby-advisory-db/COMMIT)" = 44784c295391577f25d198a9205eae4ba73ec4da \
 && printf 'ruby-advisory-db: %s advisories over %s gems at %s\n' \
      "$(find /opt/ruby-advisory-db/gems -name '*.yml' | wc -l)" \
      "$(find /opt/ruby-advisory-db/gems -mindepth 1 -maxdepth 1 -type d | wc -l)" \
      "$(cat /opt/ruby-advisory-db/COMMIT)"

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
 && for private in golden overlays step cvh warm extra; do \
        chmod 0700 "/tests/$private"; \
    done
WORKDIR /app
