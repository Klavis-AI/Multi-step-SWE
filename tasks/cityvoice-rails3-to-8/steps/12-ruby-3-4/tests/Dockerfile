# Step 12 verifier -- Ruby 3.4.10 / Rails 8.1.
#
# The first image below step 8's to move the interpreter, and the last one in
# the ladder that moves it at all.  3.4.10 is built from source by the same
# `build-ruby.sh` every other rung uses, with the same three arguments in the
# same order -- version, OpenSSL prefix, Bundler version -- so what changed
# between this file and step 11's is one version string in one RUN line and
# the two rbenv symlinks that name it.
#
# Bundler stays at 2.6.9, which is the third argument and not an accident:
# 3.4.10 ships its own Bundler, `gem install bundler -v 2.6.9` overrides it,
# and the reason is the lockfile's BUNDLED WITH trailer.  Every step from 4
# on resolves with 2.6.9; a verifier whose Bundler differed from the one the
# lockfile names would rewrite that trailer under the scorer and hand the
# candidate a diff nobody asked for.
#
# The warm bundle is this step's own, and the delta is the step: 152 specs
# where step 11 has 149, the three being `csv 3.3.2`, `mutex_m 0.3.0` and
# `observer 0.1.2` -- the packages 3.4 stops bundling with the interpreter.
# All three are pinned to
# the versions 3.4.10 itself ships, so declaring them downloads nothing and
# changes no behaviour; what it changes is whether `bundler/setup` will let
# the program require them.  The RUBY VERSION trailer moves with the
# interpreter, `ruby 3.2.11p268` to `ruby 3.4.10p104`, and BUNDLED WITH does
# not move at all.
#
# There is no Rust compiler in `toolbase`, so the 3.4.10 this file builds has
# no YJIT: `configure` looks for rustc, does not find one, and says so.
# bookworm's own rustc 1.63.0 does build a YJIT-capable 3.4.10, but adding it
# would mean adding a Rust toolchain to the `toolbase` stage thirteen verifier
# images share and to the agent image beside it, and re-measuring every
# step's timing budget, to buy a reading the interpreter gate already
# implies.  `config.yjit` is reported by the boot probe and scored nowhere.
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
# eight interpreters these thirteen steps use.  3.4 is where it is widest.
# Three of its warnings land on this tree, measured:
#
#   bigdecimal 1.4.4 -- `undefining the allocator of T_DATA class BigDecimal`,
#   first seen at 3.2 and unchanged here;
#   redcarpet -- the same allocator line for `Redcarpet::Markdown`, blamed on
#   `lib/app_content_renderer.rb:21`, also present at 3.2;
#   ostruct -- `will no longer be part of the default gems starting from Ruby
#   4.0.0`, new at 3.4, blamed on bootsnap's require shim because bootsnap is
#   what loads it, and reached from the application's own
#   `app_content_set.rb` every boot, through the `to_prepare` block at
#   `config/initializers/app_content_set.rb:10`.
#
# The first two are handed to this step rather than created by it -- both
# already appear at 3.2.11 -- so they are floors, not points, and they are
# reported and gated nowhere.
#
# The third is this step's own, and `-W0` interacts with it in a way that had
# to be measured rather than assumed: the allocator lines survive `-W0`, and
# the bundled-gem notice does NOT.  `-W0` leaves
# `$VERBOSE` at nil, and rubygems emits that notice only when `$VERBOSE` is
# non-nil.  So a gate reading it from inside this image would have read an
# empty list on a tree that had not been fixed at all -- four free points.
# `cvgate/bootprobe.rb#capture_warnings` therefore restores `$VERBOSE = false`
# for the length of its own boot and nothing else, which puts the notice back
# without turning verbose mode on; measured either way, the
# unfixed tree reports `['ostruct']` and the fixed one `[]`.
#
# `-W0` itself stays, because the baseline was recorded with it and removing
# it here would make the scorer's stdout differ from the recorder's for
# reasons no step asked for.
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
