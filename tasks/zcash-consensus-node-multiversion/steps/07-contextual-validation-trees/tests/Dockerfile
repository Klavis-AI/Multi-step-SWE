# Verifier image for step 07 of zcash-consensus-node-multiversion.
#
# Built with this step's tests/ directory as the Docker build context: it
# cannot reach the task root, environment/, or another step, so helpers.sh and
# the drivers are copied into every step. Harbor restores the collected /app
# artifact into this image and runs /tests/test.sh.
#
# The base image is the one contemporary with this step's upstream release.
# There is no single toolchain that compiles all fourteen: `lexical-core 0.7.x`
# -- a host build-dependency of librocksdb-sys, reached through bindgen -- does
# not compile on rustc newer than 1.52, and the last releases in the ladder are
# edition 2021, which needs 1.56 or newer. The two ranges are disjoint.
FROM rust:1.59-bullseye

# Everything from here to the step-identity line is byte-identical across every
# step image in this band, so the daemon builds it once and every later step
# reuses the layer. That matters: on a cargo this old the crates.io index is a
# 560 MB git clone that takes about a quarter of an hour.
#
# Both bases sit on a Debian suite that is out of support, and the two are out
# of support in different ways, so the package set is installed through a
# ladder rather than a single sed.
#
#   buster       deb.debian.org no longer serves the suite at all, so
#                `apt-get update` itself fails.
#   bullseye     deb.debian.org still serves bullseye and bullseye-updates and
#                security.debian.org still serves the bullseye-security *index*,
#                but the pool behind it has been withdrawn -- so `apt-get
#                update` succeeds and the install then 404s on every package
#                that has a security version, which is most of them.
#
# Hence: try the image's own sources first, and only on failure rewrite
# sources.list wholesale to archive.debian.org. Rewriting wholesale rather than
# sed-ing deb.debian.org matters because the naive substitution produces
# archive.debian.org/debian-security/debian-security, which also 404s. The
# security suite is appended only if it is really there -- archive.debian.org
# carries buster/updates but has no bullseye-security at all, and bullseye's
# final point release folded those fixes into main anyway.
RUN set -eux; \
    PKGS="bash build-essential ca-certificates clang cmake coreutils curl \
          diffutils gawk git jq libclang-dev libssl-dev llvm-dev pkg-config \
          procps python3 zlib1g-dev"; \
    . /etc/os-release; \
    from_archive() { \
        printf 'deb http://archive.debian.org/debian %s main\n' "$VERSION_CODENAME" \
            > /etc/apt/sources.list; \
        for suite in "$VERSION_CODENAME/updates" "$VERSION_CODENAME-security"; do \
            if curl -sfI "http://archive.debian.org/debian-security/dists/$suite/InRelease" \
                   > /dev/null; then \
                printf 'deb http://archive.debian.org/debian-security %s main\n' "$suite" \
                    >> /etc/apt/sources.list; \
            fi; \
        done; \
        rm -f /etc/apt/sources.list.d/*.list /etc/apt/sources.list.d/*.sources || true; \
        echo 'Acquire::Check-Valid-Until "false";' > /etc/apt/apt.conf.d/99no-check-valid; \
        apt-get update; \
        apt-get install -y --no-install-recommends $PKGS; \
    }; \
    if apt-get update 2>/dev/null && apt-get install -y --no-install-recommends $PKGS; \
    then :; else from_archive; fi; \
    command -v clang; command -v cmake; command -v git; command -v jq; command -v python3; \
    rm -rf /var/lib/apt/lists/*

# Pin libclang explicitly. bindgen -- used by librocksdb-sys and zcash_script --
# discovers libclang by scanning, and picks up a too-new one when several are
# installed, which makes it emit an identifier containing a '/' and panic with
# "is not a valid Ident". The failure is nondeterministic across builds on the
# same source, so the variable is set rather than left to discovery. The
# llvm-config warning bindgen prints without LLVM_CONFIG_PATH is harmless, but
# setting it keeps the log clean.
ENV CARGO_TERM_COLOR=never \
    CARGO_TARGET_DIR=/cache/target \
    LIBCLANG_PATH=/usr/lib/llvm-11/lib \
    LLVM_CONFIG_PATH=/usr/bin/llvm-config \
    RUSTUP_TOOLCHAIN=1.59.0

# Warm the registry index in a shared layer, so the per-step fetch below only
# downloads .crate files. The dependency named here is irrelevant; cloning the
# index is the whole point -- on cargo 1.5x that is a full git clone of
# crates.io-index (~560 MB), and it is byte-identical work for every step in a
# band, so it belongs in a layer that lands before the step-identity line.
#
# The version is pinned to a release contemporaneous with the toolchain: an
# unpinned `libc = "0.2"` resolves to a present-day release whose manifest uses
# keys cargo 1.52 cannot parse, and the resolver then aborts *after* the clone.
# The `|| true` is belt and braces -- by the time resolution can fail, the index
# is already on disk, which is all this layer is for.
RUN set -eux; \
    mkdir -p /tmp/idx/src; cd /tmp/idx; \
    printf '[package]\nname = "idx"\nversion = "0.0.0"\nedition = "2018"\n[dependencies]\nlibc = "=0.2.93"\n' > Cargo.toml; \
    : > src/lib.rs; \
    cargo fetch || true; \
    test -d "${CARGO_HOME:-/usr/local/cargo}/registry"; \
    cd /; rm -rf /tmp/idx

# Band-wide dependency prebuild.
#
# This is the expensive layer -- roughly a quarter of an hour of rustc, most of
# it librocksdb-sys, zcash_script and secp256k1-sys -- and it is the same work
# for every step in a band. It has to land BEFORE the first COPY to be shared:
# each step's tests/ is its own build context, and every step's files carry a
# distinct mtime (the builder keys a context file by path, size and mtime and
# never reads its bytes, so identical mtimes are how one step's tests end up
# baked into another step's image). That makes
# every COPY from tests/ step-unique by construction, and anything downstream of
# one is rebuilt per step. Fetching the skeleton over the network instead keeps
# the instruction byte-identical across the band, so the daemon builds it once.
#
# What is built is a cargo-chef style skeleton: the band tag's workspace
# manifests and lockfile with every source file replaced by an empty stub, so
# the whole third-party graph compiles and no upstream source is retained. The
# per-step skeleton below then fills in whatever this tag's lockfile pins
# differently, which is cheap once the natives are warm.
#
# The stub members' own artifacts are deleted afterwards, so a candidate crate
# can never link against an empty stub of itself.
RUN set -eux; \
    git clone --quiet --depth 1 --branch v1.0.0-beta.1 \
        https://github.com/ZcashFoundation/zebra /tmp/band; \
    cd /tmp/band; rm -rf .git; \
    for f in Cargo.toml */Cargo.toml; do \
        awk '/^\[\[bench\]\]/{skip=1;next} /^\[/{skip=0} !skip' "$f" > "$f.skel"; \
        mv "$f.skel" "$f"; \
    done; \
    find . -name '*.rs' -delete; \
    : > MEMBERS; \
    for d in */; do \
        d=${d%/}; [ -f "$d/Cargo.toml" ] || continue; \
        mkdir -p "$d/src" "$d/tests"; \
        : > "$d/src/lib.rs"; printf 'fn main() {}\n' > "$d/src/main.rs"; \
        : > "$d/tests/warm.rs"; \
        echo "$d" >> MEMBERS; \
    done; \
    cargo fetch; \
    (cargo build --workspace --all-targets --offline --jobs 4 || cargo build --workspace --all-targets --jobs 4 || true); \
    while read -r m; do \
        u=$(printf '%s' "$m" | tr - _); \
        rm -rf /cache/target/debug/.fingerprint/"$m"-* \
               /cache/target/debug/deps/lib"$u"-* \
               /cache/target/debug/deps/"$u"-*; \
    done < MEMBERS; \
    rm -rf /cache/target/debug/.fingerprint/warm-* /cache/target/debug/deps/warm-*; \
    cd /; rm -rf /tmp/band; \
    du -sh /cache/target "${CARGO_HOME:-/usr/local/cargo}"/registry

RUN mkdir -p /app /logs/verifier /cache/target

# Step identity, for the binding check at the top of test.sh.
#
# Written BEFORE the COPY below, so it names this IMAGE even when the copy is
# served from another step's cache entry. The builder identifies a build-context
# file by (relative path, size, mtime) and never reads its bytes, so two steps
# whose tests/ trees share a filename, a size and a second are indistinguishable
# to it -- and the wrong milestone's verifier gets baked in at an ordinary-looking
# score. Nothing inside a Dockerfile prevents that; this only reports it.
#
# Written as a LITERAL rather than as an ARG: harbor builds the verifier
# through docker-compose-build.yaml, which
# declares no `args:`, so no --build-arg can reach this build and an ARG default
# would be an override surface nothing can use.
#
# Keep this number, the directory's number and test.sh's check equal.
RUN echo "07" > /etc/harbor-step-id

# The toolchain this step's upstream release was contemporary with. test.sh
# also exports it as RUSTUP_TOOLCHAIN, which overrides any rust-toolchain file
# the candidate may have left in /app, so grading always uses the era compiler.
RUN echo "1.59.0" > /etc/harbor-rust-toolchain

COPY . /tests
RUN chmod +x /tests/test.sh

# Dependency pre-build.
#
# /tests/assets/prefetch/skeleton is every workspace manifest of this step's
# pinned release plus its lockfile, with every source file replaced by an empty
# stub, so no upstream source ships. Building it here compiles the entire
# third-party graph, including the expensive native ones (librocksdb-sys is
# nearly seven minutes on its own), into CARGO_TARGET_DIR. At grading time only
# the candidate's own crates are compiled, and `--offline` resolves.
#
# Every shape the verifier will actually issue is warmed here, not just the wide
# one. Asking cargo for a NARROWER unit graph than the one that was baked is not
# a subset: dropping a member drops its dependencies out of feature unification,
# which changes the `-C metadata` hash of everything downstream and recompiles
# it. Measured on this tree: an image baked with `--workspace` and then queried
# with `-p zebra-state` rebuilt 44 crates in 682 s -- bindgen's features moved,
# so librocksdb-sys and rocksdb moved with them -- while the same image queried
# with the shape it was baked with finished in 2 s having compiled nothing.
#
# So the shapes are read out of this step's own groups.txt: every `@build`
# group's cargo invocation verbatim, and `-p <crate> --tests` for every crate a
# driver group is injected into. Each `|| true` is deliberate; a shape that
# cannot be warmed should cost grading time, not fail the image build.
#
# The stub members' own artifacts are deleted afterwards, so a candidate crate
# can never link against an empty stub of itself.
#
# The skeleton is checksummed here, against assets/PREFETCH.sha256, rather than
# by the grade-time SHA256SUMS: this RUN deletes the whole subtree when it is
# done, so a grade-time manifest that listed it would fail the asset gate on
# every group. Checking it here covers the same bytes at the point they are
# actually used.
RUN set -eux; \
    ( cd /tests/assets && sha256sum -c --quiet PREFETCH.sha256 ); \
    cd /tests/assets/prefetch/skeleton; \
    cargo fetch; \
    (cargo build --workspace --all-targets --offline --jobs 4 || cargo build --workspace --all-targets --jobs 4 || true); \
    awk -F'\t' '$4=="@build"{print $5}' /tests/assets/groups.txt \
      | while read -r sel; do (cargo $sel --offline --jobs 4 || true); done; \
    awk -F'\t' 'NF>=4 && $4!="@build" && $1 !~ /^#/ {print $4}' /tests/assets/groups.txt \
      | sort -u \
      | while read -r c; do (cargo build --offline --jobs 4 -p "$c" --tests || true); done; \
    while read -r m; do \
        u=$(printf '%s' "$m" | tr - _); \
        rm -rf /cache/target/debug/.fingerprint/"$m"-* \
               /cache/target/debug/deps/lib"$u"-* \
               /cache/target/debug/deps/"$u"-* ; \
    done < MEMBERS; \
    rm -rf /cache/target/debug/.fingerprint/warm-* /cache/target/debug/deps/warm-*; \
    cd /; rm -rf /tests/assets/prefetch /tests/assets/PREFETCH.sha256; \
    du -sh /cache/target "${CARGO_HOME:-/usr/local/cargo}"/registry

WORKDIR /app
