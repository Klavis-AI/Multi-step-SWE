#!/usr/bin/env bash
# Bring up the local PostgreSQL service, then hand over to the container
# command.  This is the "where to run" hook for the database: the application's
# own config/database.yml.example connects as the `postgres` role over the
# local socket, so the cluster simply has to be running, the way it is on a
# developer's machine.
set -u

if ! pg_isready -q 2>/dev/null; then
    mkdir -p /var/run/postgresql
    chown postgres:postgres /var/run/postgresql
    pg_ctlcluster 16 main start >/dev/null 2>&1 \
        || su postgres -c "/usr/lib/postgresql/16/bin/pg_ctl -D /var/lib/postgresql/16/main \
             -o '-c config_file=/etc/postgresql/16/main/postgresql.conf' \
             -l /var/log/postgresql/startup.log -w start" >/dev/null 2>&1 \
        || echo "entrypoint: PostgreSQL did not start" >&2
fi

exec "$@"
