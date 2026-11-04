# Agent environment: the WebGoatPHP deployment as it stands before the
# migration -- Apache 2.4 + PHP 7.4 serving the repository from the document
# root, backed by MySQL 5.7.44 running with the permissive sql_mode this
# deployment has always used. PostgreSQL 17 is installed but has no cluster yet.
#
# WebGoatPHP is pinned at commit f3b80714fbb2c5adbe6d1bb3f81db914b734336e
# (2016-10-12) and vendored as webgoat-src.tar.gz so no build step needs GitHub.

FROM mysql:5.7.44 AS mysql57

FROM debian:bookworm-slim

ENV DEBIAN_FRONTEND=noninteractive
ENV LANG=C.UTF-8
ENV TZ=UTC

# Fixed uid/gid for postgres and mysql: the PostgreSQL data directory travels
# with /app between containers, and PostgreSQL refuses to start on a directory
# it does not own.
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
      pgloader \
      faketime \
      libaio1 libnuma1 libgssapi-krb5-2 libkrb5-3 libk5crypto3 libcom-err2 \
      libkeyutils1 libselinux1 zlib1g \
      git procps psmisc ripgrep jq vim-tiny less unzip xz-utils patch rsync \
      python3 python3-pip locales netcat-openbsd iproute2 \
 && rm -rf /var/lib/apt/lists/*

# Agent harnesses need node and npm on PATH; without them Harbor's setup runs
# `apt-get install nodejs npm` (no --no-install-recommends) into this image.
# curl, bash and pgrep come from the apt block, node and npm from this block.
# Keep this block (or anything else that provides `node` and `npm`) unless the
# task deliberately wants Debian's node packages.
ARG NODE_VERSION=24.22.0
RUN set -eux; \
    arch="$(dpkg --print-architecture)"; \
    case "$arch" in \
      amd64) node_arch=x64 ;; \
      arm64) node_arch=arm64 ;; \
      *) echo "unsupported architecture: $arch" >&2; exit 1 ;; \
    esac; \
    tarball="node-v${NODE_VERSION}-linux-${node_arch}.tar.xz"; \
    curl -fsSLo "/tmp/${tarball}" "https://nodejs.org/dist/v${NODE_VERSION}/${tarball}"; \
    curl -fsSLo /tmp/SHASUMS256.txt "https://nodejs.org/dist/v${NODE_VERSION}/SHASUMS256.txt"; \
    (cd /tmp && grep " ${tarball}\$" SHASUMS256.txt | sha256sum -c -); \
    tar -xJf "/tmp/${tarball}" -C /usr/local --strip-components=1 --no-same-owner; \
    rm -f "/tmp/${tarball}" /tmp/SHASUMS256.txt; \
    test ! -e /usr/share/nodejs; \
    node --version; npm --version

# MySQL 5.7.44. The Debian archive carries no 5.7 packages, so the server, the
# client tools and the Oracle Linux 7 libraries they need are transplanted from
# the official mysql:5.7.44 image into /opt/mysql57.
COPY --from=mysql57 /usr/sbin/mysqld /opt/mysql57/bin/mysqld
COPY --from=mysql57 /usr/bin/mysql /usr/bin/mysqldump /usr/bin/mysqladmin \
     /usr/bin/mysql_upgrade /usr/bin/my_print_defaults /usr/bin/mysqlpump \
     /opt/mysql57/bin/
COPY --from=mysql57 /usr/share/mysql /opt/mysql57/share/mysql
COPY --from=mysql57 /lib64/libssl.so.10 /lib64/libcrypto.so.10 \
     /lib64/libncurses.so.5 /lib64/libtinfo.so.5 /lib64/libpcre.so.1 \
     /lib64/libfreebl3.so /opt/mysql57/lib/

# Wrappers rather than a container-wide LD_LIBRARY_PATH, so the Oracle Linux
# libraries above stay invisible to everything that is not MySQL.
RUN set -eux; \
    for tool in mysql mysqldump mysqladmin mysql_upgrade mysqlpump my_print_defaults; do \
      printf '#!/bin/sh\nexport LD_LIBRARY_PATH=/opt/mysql57/lib\nexec /opt/mysql57/bin/%s "$@"\n' \
        "$tool" > "/usr/local/bin/$tool"; \
      chmod 755 "/usr/local/bin/$tool"; \
    done; \
    printf '#!/bin/sh\nexport LD_LIBRARY_PATH=/opt/mysql57/lib\nexec /opt/mysql57/bin/mysqld "$@"\n' \
      > /usr/local/sbin/mysqld; \
    chmod 755 /usr/local/sbin/mysqld; \
    mysql --version; mysqld --version

COPY files/my.cnf /etc/mysql/my.cnf
COPY files/initd-mysql /etc/init.d/mysql
RUN chmod 755 /etc/init.d/mysql

# PostgreSQL 17: keep the server and its tools, drop the cluster Debian's
# postinst creates. This deployment's cluster lives in the data directory named
# in the task instruction, and is created there.
ENV PATH="/usr/lib/postgresql/17/bin:${PATH}"
RUN pg_dropcluster 17 main --stop \
 && psql --version && initdb --version && pgloader --version \
 && mkdir -p /etc/postgresql /var/log/postgresql /var/run/postgresql \
 && chown postgres:postgres /var/log/postgresql /var/run/postgresql

# The step boundary stops the servers so their data directories can be copied
# out intact, and starts them again before the next step. This is the script it
# runs, and the one the instruction points at.
COPY files/wg-services /usr/local/sbin/wg-services
RUN chmod 755 /usr/local/sbin/wg-services

# Apache: the repository is the document root, .htaccess is honoured, and the
# server runs with a frozen clock so the application's own jf::time() is
# deterministic. Nothing but the application's own interpreter is preloaded, so
# the agent's network tools keep the real date.
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

# The same clock for PHP started from a shell. Apache's envvars reach mod_php
# and nothing else, and the application is also run through its CLI front
# controller, against the same database. files/php-frozen carries the reasoning
# and the measurement; the short version is that the settings store deletes its
# expired rows on a one-in-ten draw, and under the host's date all but five of
# the seeded jf_options rows are expired. `date`, curl and the agent's other
# tools are untouched -- only the interpreter moves to 2026-01-01.
COPY files/php-frozen /usr/local/bin/php
RUN set -eux; \
    chmod 755 /usr/local/bin/php; \
    ln -sf php /usr/local/bin/php7.4; \
    test "$(command -v php)" = /usr/local/bin/php; \
    test "$(php -r 'echo time();')" = 1767225600; \
    test "$(php7.4 -r 'echo date("Y-m-d H:i:s");')" = '2026-01-01 00:00:00'

COPY webgoat-src.tar.gz /tmp/webgoat-src.tar.gz
COPY files/install-app.sh /tmp/install-app.sh
RUN chmod 755 /tmp/install-app.sh && /tmp/install-app.sh && rm -f /tmp/install-app.sh

COPY files/init-db.sh /tmp/init-db.sh
COPY files/seed /tmp/seed
RUN chmod 755 /tmp/init-db.sh && /tmp/init-db.sh && rm -rf /tmp/init-db.sh /tmp/seed

# mini-swe-agent's setup probes for gcc and make and, if either is missing,
# runs `apt-get install build-essential` (no --no-install-recommends) into this
# image. Pre-installed for the agent harness only; no step builds native code.
# A small late layer rather than an edit of the apt block above, so the PHP,
# PostgreSQL and MySQL layers and the seeded database keep their cache keys.
RUN apt-get update \
 && apt-get install -y --no-install-recommends build-essential \
 && rm -rf /var/lib/apt/lists/* \
 && gcc -dumpfullversion \
 && make --version >/dev/null

WORKDIR /app
COPY timer.sh /app/timer.sh
RUN chmod +x /app/timer.sh
