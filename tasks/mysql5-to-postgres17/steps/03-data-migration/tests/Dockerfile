# Verifier image for a step that is still scored against both databases
# (steps 01-03: the application keeps running on MySQL while the PostgreSQL
# cluster, schema and data are built alongside it).
#
# Built with steps/<step>/tests/ as the Docker build context, which can reach
# neither the task root nor another step, so everything the scorer needs is
# copied into each step by authoring/sync_tests.py. Edit
# authoring/common/tests/Dockerfile.mysql, never a copy.
#
# Nothing here is inherited from the agent's image. The verifier installs its
# own Apache, PHP, MySQL and PostgreSQL, and serves the candidate's tree from
# its own pristine /etc/apache2 and /etc/php -- which is what keeps the frozen
# clock and the document root out of the candidate's reach (NOTES §15).

FROM mysql:5.7.44 AS mysql57

FROM debian:bookworm-slim

ENV DEBIAN_FRONTEND=noninteractive
ENV LANG=C.UTF-8
ENV TZ=UTC

# Same fixed uid/gid as the agent's image: the data directories travel between
# containers, and PostgreSQL refuses to start on a directory it does not own.
RUN groupadd -g 5432 postgres \
 && useradd -u 5432 -g 5432 -M -d /var/lib/postgresql -s /bin/bash postgres \
 && groupadd -g 3306 mysql \
 && useradd -u 3306 -g 3306 -M -d /var/lib/mysql -s /usr/sbin/nologin mysql

RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      ca-certificates curl wget gnupg lsb-release apt-transport-https \
 && curl -fsSL https://packages.sury.org/php/apt.gpg \
      -o /usr/share/keyrings/sury-php.gpg \
 && echo "deb [signed-by=/usr/share/keyrings/sury-php.gpg] https://packages.sury.org/php/ bookworm main" \
      > /etc/apt/sources.list.d/sury-php.list \
 && install -d /usr/share/postgresql-common/pgdg \
 && curl -fsSL https://www.postgresql.org/media/keys/ACCC4CF8.asc \
      -o /usr/share/postgresql-common/pgdg/apt.postgresql.org.asc \
 && echo "deb [signed-by=/usr/share/postgresql-common/pgdg/apt.postgresql.org.asc] http://apt.postgresql.org/pub/repos/apt bookworm-pgdg main" \
      > /etc/apt/sources.list.d/pgdg.list \
 && apt-get update \
 && apt-get install -y --no-install-recommends \
      apache2 libapache2-mod-php7.4 \
      php7.4-cli php7.4-mysql php7.4-pgsql php7.4-mbstring php7.4-xml \
      php7.4-curl php7.4-intl php7.4-gd php7.4-zip php7.4-bcmath php7.4-opcache \
      postgresql-17 postgresql-client-17 postgresql-contrib-17 \
      faketime \
      libaio1 libnuma1 libgssapi-krb5-2 libkrb5-3 libk5crypto3 libcom-err2 \
      libkeyutils1 libselinux1 zlib1g \
      util-linux procps psmisc netcat-openbsd locales \
      python3 python3-pip \
 && rm -rf /var/lib/apt/lists/*

# The scorer's own libraries: a PostgreSQL driver, a MySQL driver, and the
# PostgreSQL grammar the dialect gate parses with. Pinned, and installed at
# build time, so grading never depends on a package index being reachable.
RUN pip3 install --break-system-packages --no-cache-dir \
      pglast==8.4 psycopg2-binary==2.9.13 PyMySQL==1.2.0

# PostgreSQL 17: the server and its tools, without the cluster Debian's
# postinst creates. The cluster under grading is the candidate's, restored from
# the artifact.
#
# The MySQL runtime directories are created here too, with the same owners and
# modes the agent's image gives them. /etc/mysql travels as an artifact, so the
# server this image starts is configured by the candidate's my.cnf -- and the
# supplied one names /var/lib/mysql-files and /var/log/mysql. A verifier image
# without them aborts mysqld at startup ("Failed to access directory for
# --secure-file-priv"), which scores a correct deployment 0. Measured on
# oracle-v1, 2026-09-16; see NOTES-authoring.md §19.8.
ENV PATH="/usr/lib/postgresql/17/bin:${PATH}"
RUN pg_dropcluster 17 main --stop \
 && psql --version && initdb --version \
 && mkdir -p /app /logs/verifier /etc/mysql /etc/postgresql \
             /var/lib/mysql /var/lib/postgresql /var/log/postgresql \
             /var/run/postgresql /var/lib/mysql-files /var/log/mysql \
 && chown postgres:postgres /var/log/postgresql /var/run/postgresql \
 && chown mysql:mysql /var/lib/mysql-files /var/log/mysql \
 && chmod 750 /var/lib/mysql-files

# Apache and PHP, the scorer's copies. The document root, the rewrite rules and
# the frozen clock are identical to the agent image's, and are the only part of
# the deployment the candidate cannot change.
COPY files/webgoat.apache.conf /etc/apache2/sites-available/000-default.conf
COPY files/zz-webgoat.ini /etc/php/7.4/apache2/conf.d/zz-webgoat.ini
COPY files/zz-webgoat.ini /etc/php/7.4/cli/conf.d/zz-webgoat.ini
RUN set -eux; \
    a2enmod rewrite; \
    printf 'Listen 127.0.0.1:80\n' > /etc/apache2/ports.conf; \
    { \
      echo ''; \
      echo '# Frozen clock for the application server.'; \
      echo 'export LD_PRELOAD=/usr/lib/x86_64-linux-gnu/faketime/libfaketime.so.1'; \
      echo 'export FAKETIME="@2026-01-01 00:00:00 x0"'; \
      echo 'export FAKETIME_DONT_FAKE_MONOTONIC=1'; \
      echo 'export TZ=UTC'; \
    } >> /etc/apache2/envvars; \
    test -e /usr/lib/x86_64-linux-gnu/faketime/libfaketime.so.1

# The same clock for PHP started from a shell, because the scorer reaches the
# application that way: ready.app_ready() runs the CLI front controller against
# the restored database. files/php-frozen carries the reasoning; the short
# version is that the settings store deletes its expired rows on a one-in-ten
# draw inside every load and save, and under the host's date all but five of
# the seeded jf_options rows are expired -- which moved behavior:tables from
# 50/50 to 48/50 on one oracle run and left it at 50/50 on the four before it.
# ready.py sets the same variables on the probe itself; a deployment whose
# interpreter carries its own clock does not depend on every caller knowing to.
COPY files/php-frozen /usr/local/bin/php
RUN set -eux; \
    chmod 755 /usr/local/bin/php; \
    ln -sf php /usr/local/bin/php7.4; \
    test "$(command -v php)" = /usr/local/bin/php; \
    test "$(php -r 'echo time();')" = 1767225600; \
    test "$(php7.4 -r 'echo date("Y-m-d H:i:s");')" = '2026-01-01 00:00:00'

# MySQL 5.7.44, transplanted from the official image exactly as the agent's
# image does it. Steps 01-03 are scored with the application still on MySQL, so
# the scorer has to be able to start the candidate's data directory.
COPY --from=mysql57 /usr/sbin/mysqld /opt/mysql57/bin/mysqld
COPY --from=mysql57 /usr/bin/mysql /usr/bin/mysqldump /usr/bin/mysqladmin \
     /usr/bin/my_print_defaults /opt/mysql57/bin/
COPY --from=mysql57 /usr/share/mysql /opt/mysql57/share/mysql
COPY --from=mysql57 /lib64/libssl.so.10 /lib64/libcrypto.so.10 \
     /lib64/libncurses.so.5 /lib64/libtinfo.so.5 /lib64/libpcre.so.1 \
     /lib64/libfreebl3.so /opt/mysql57/lib/
RUN set -eux; \
    for tool in mysql mysqldump mysqladmin my_print_defaults; do \
      printf '#!/bin/sh\nexport LD_LIBRARY_PATH=/opt/mysql57/lib\nexec /opt/mysql57/bin/%s "$@"\n' \
        "$tool" > "/usr/local/bin/$tool"; \
      chmod 755 "/usr/local/bin/$tool"; \
    done; \
    printf '#!/bin/sh\nexport LD_LIBRARY_PATH=/opt/mysql57/lib\nexec /opt/mysql57/bin/mysqld "$@"\n' \
      > /usr/local/sbin/mysqld; \
    chmod 755 /usr/local/sbin/mysqld; \
    mysql --version; mysqld --version

# The scorer. wglib is the shared instrument set, fixtures are the recordings
# it scores against, and plan.py is this step's own -- it names the families
# that run and what each one weighs.
COPY wglib /tests/wglib
COPY fixtures /tests/fixtures
COPY helpers.sh test.sh plan.py /tests/
# Compile the scorer at build time: a syntax error in a family module would
# otherwise surface as a 0 on a candidate who did the work. PYTHONPYCACHEPREFIX
# keeps the .pyc files out of /tests, which is this image's build context
# (trap 8), and they are swept anyway.
#
# The four PHP instruments are run as `nobody` -- `sqlgate.run_extractor`,
# `mysqlgone.scan`, `retire.scan` and `retire.wiringscan` drop privileges
# before letting a parser read candidate code. COPY carries the authoring tree's own modes, which are
# 0600 on files and 0700 on directories, and under those that account cannot
# read the program it is being told to run: php exits 1 with "Could not open
# input file" on *stdout*, and the family reports a failed selftest for every
# tree. So open exactly what the parser needs -- the four programs, the
# php-parser vendor tree, and the selftest corpus -- and nothing else.
# /tests/fixtures holds the recordings every family scores against and the rest
# of /tests/wglib is the scorer itself; both stay 0600 root, because the
# candidate's own PHP runs in this container. Round-3 migration review,
# 2026-10-05: those modes are now set here rather than inherited -- git keeps
# no mode but the execute bit, so a checkout made under an ordinary umask
# builds every recording and the scorer world-readable (measured: www-data
# read /tests/fixtures/textvalues-repaired.json in an image built from this
# repository). The two `find`s below reproduce the authoring tree's 0600
# files and 0700 directories under the three Docker-created ones.
#
# `$PHP_INSTRUMENTS` is one list, used for the lint, for the chmod and for the
# run, so that adding an instrument and forgetting one of the three is not a
# thing that can happen. It already did: classmap.php arrived for step 05 and
# the chmod list still named two files, so `nobody` could not open it, both
# wiring families failed closed on every tree including the golden one, and
# `migrated:wiring-intact` is a guard -- a zero multiplier, so a zero reward
# for every submission. wiringmap.php arrived the same way in §24.15 and is
# on the list from its first commit.
#
# Then run each instrument over the selftest corpus *as that account*, at build
# time -- first bare, which catches a parse error or an unreadable file, and
# then through the two modules' own `selftest()`, which is the check with
# teeth: it asserts the instrument finds the planted dirt and does not find
# anything in the clean fixture. Those two modules are `mysqlgone` and
# `retire`, and from step 05 onward is where they ship (`HELD_UNTIL` in
# sync_tests.py), so the loop skips a module this step was not given rather
# than failing the build for the four steps that do not score against it. Verifying the instrument before the finding is
# the point (trap 6): the failure this replaces was a full-denominator crash of
# `migrated:dialect` on a tree that deserved 43/43, and nothing in the image
# said so until the report did.
RUN set -eux; \
    find /tests -mindepth 1 -type f -exec chmod go-rwx {} +; \
    find /tests -mindepth 2 -type d -exec chmod go-rwx {} +; \
    chmod 755 /tests/test.sh; \
    PHP_INSTRUMENTS='sqlextract driverscan classmap wiringmap'; \
    for prog in $PHP_INSTRUMENTS; do \
        php -l "/tests/wglib/$prog.php"; \
        chmod 644 "/tests/wglib/$prog.php"; \
    done; \
    chmod -R a+rX /tests/wglib/vendor /tests/wglib/selftest; \
    nobody_uid="$(id -u nobody)"; nobody_gid="$(id -g nobody)"; \
    for prog in $PHP_INSTRUMENTS; do \
        setpriv --reuid "$nobody_uid" --regid "$nobody_gid" --clear-groups \
            env HOME=/tmp TMPDIR=/tmp \
            php "/tests/wglib/$prog.php" /tests/wglib/selftest > /dev/null; \
    done; \
    for mod in mysqlgone retire; do \
        [ -f "/tests/wglib/$mod.py" ] || continue; \
        PYTHONPATH=/tests PYTHONPYCACHEPREFIX=/tmp/pyc python3 -c \
            "from wglib import $mod; print($mod.selftest())"; \
    done; \
    PYTHONPYCACHEPREFIX=/tmp/pyc python3 -m compileall -q /tests/wglib /tests/plan.py; \
    rm -rf /tmp/pyc; \
    find /tests -name '__pycache__' -prune -exec rm -rf {} +

WORKDIR /app
