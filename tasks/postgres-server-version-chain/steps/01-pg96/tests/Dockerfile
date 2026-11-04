# Verifier image: the same client programs, a pristine C toolchain used to
# rebuild the candidate from source, and the test corpus.  No PostgreSQL
# server binary exists in this image, so a candidate that wrapped one would
# have nothing to wrap.
#
# Parity with the agent image is part of the contract: the candidate builds and
# runs its server there and is rebuilt and run here, so anything reachable at
# runtime in one image has to be reachable in the other.  `gdb`/`valgrind` in
# the agent image pull in `libpython3.11`, and `pkg-config` sits in its
# toolchain list; both are named explicitly here so the two images cannot drift
# apart through a transitive dependency nobody is looking at.
FROM debian:bookworm-slim
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
      bash ca-certificates curl gnupg coreutils diffutils gawk procps \
      build-essential make pkg-config libssl-dev python3 python3-pip locales tzdata file \
      libpython3.11 \
 && install -d /usr/share/postgresql-common/pgdg \
 && curl -fsSL -o /usr/share/postgresql-common/pgdg/apt.postgresql.org.asc \
      https://www.postgresql.org/media/keys/ACCC4CF8.asc \
 && echo "deb [signed-by=/usr/share/postgresql-common/pgdg/apt.postgresql.org.asc] https://apt.postgresql.org/pub/repos/apt bookworm-pgdg main 18 9.6" \
      > /etc/apt/sources.list.d/pgdg.list \
 && apt-get update \
 && apt-get install -y --no-install-recommends postgresql-client-18 postgresql-client-9.6 \
 && rm -rf /var/lib/apt/lists/* \
 && rm -f /usr/lib/postgresql/*/bin/postgres /usr/lib/postgresql/*/bin/postmaster \
          /usr/lib/postgresql/*/bin/initdb /usr/lib/postgresql/*/bin/pg_ctl 2>/dev/null || true
RUN pip3 install --break-system-packages --no-cache-dir "psycopg[binary]==3.2.3"
ENV TZ=UTC PGTZ=UTC LANG=C LC_ALL=C
RUN mkdir -p /app /logs/verifier
# The candidate's server runs as this unprivileged user, never as root, so that
# it cannot read the expectations it is being checked against.  Byte-exact
# comparison is what makes this matter: a "server" that found the recorded
# output of the case being replayed could emit it and implement nothing.
RUN useradd --system --uid 4242 --create-home --home-dir /home/pgvc --shell /usr/sbin/nologin pgvc
COPY . /tests
RUN chmod +x /tests/test.sh \
 && chown -R root:root /tests \
 && chmod -R go-rwx /tests
WORKDIR /app
