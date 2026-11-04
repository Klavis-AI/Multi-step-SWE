# Verifier image for step 03 of turso-engine-multiversion.
#
# Pinned by digest: the corpus and the differential oracle must be the same
# bytes at every step and on every rebuild.
FROM gcc:12-bookworm@sha256:47d8aaf12eec4c8b23092c158ee44b6305b801a352121d50f960ba8d108b7433

ENV LC_ALL=C LANG=C TZ=UTC SOURCE_DATE_EPOCH=1700000000

# Fail the image build, not the trial, if a tool the harness needs is absent.
RUN set -eu; for t in bash sha256sum cmp find sort awk sed grep timeout gcc; do \
        command -v "$t" >/dev/null 2>&1 || { echo "missing tool: $t" >&2; exit 1; }; \
    done

RUN mkdir -p /app /logs/verifier

# Written BEFORE the first COPY so that the step identity is part of a layer
# that cannot be shared with another step, whatever the build context cache
# decides about the files copied below.
RUN echo "03" > /etc/harbor-step-id

COPY . /tests
WORKDIR /tests

# The differential oracle. Built here from the pinned public-domain
# amalgamation rather than shipped as a binary, so it matches this image's
# glibc exactly and its provenance is the SHA-256 manifest next to it.
RUN set -eu; \
    cd /tests/assets/sqlite-src; \
    sha256sum -c SHA256SUMS; \
    gcc -O1 -DSQLITE_THREADSAFE=0 -DSQLITE_OMIT_LOAD_EXTENSION \
        -DSQLITE_ENABLE_DBSTAT_VTAB -DSQLITE_DEFAULT_MEMSTATUS=0 \
        -o /tests/assets/bin/sqlite3 shell.c sqlite3.c -lm; \
    /tests/assets/bin/sqlite3 --version; \
    test "$(/tests/assets/bin/sqlite3 :memory: 'SELECT sqlite_version();')" = "3.50.2"; \
    rm -rf /tests/assets/sqlite-src

RUN chmod +x /tests/test.sh /tests/assets/lib/runner.sh
