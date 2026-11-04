#!/usr/bin/env bash
#
# Reference solution for step 02 -- "the schema exists in PostgreSQL 17".
#
# Three deliverables, in the order the step's gates read them:
#
#   1. a cluster under /var/lib/postgresql, on port 5432 and the socket
#      directory /var/run/postgresql, started by wg-services;
#   2. the `webgoatphp` database on it, holding the 27 tables;
#   3. /app/install/_db/pdo_pgsql.schema.sql, the file jFramework installs
#      from -- which is what builds (2), so the two cannot drift apart.
#
# Nothing here touches MySQL, the application's configuration, or its database
# adapter. The application keeps serving from MySQL for three more steps, and
# repointing it now would take the recorded HTTP corpus with it.
#
# Every conversion choice below is argued in NOTES-authoring.md §20.2-20.5 and
# in authoring/make_pg_schema.py, which emitted the SQL embedded at the bottom
# of this script. The four that matter:
#
#   * identifiers are folded, never quoted, because the application
#     interpolates table names into SQL unquoted;
#   * foreign keys are added at the end, because PostgreSQL honours no
#     FOREIGN_KEY_CHECKS=0 comment and the shipped MySQL file is ordered wrong;
#   * tinyint becomes smallint with a range check, not boolean, because the
#     application writes `SET Activated=1` and because strict MySQL refuses 128;
#   * the case-insensitive collations are declared per column by the file
#     itself, so the schema means the same thing on a database created
#     somewhere else with a different locale.
set -euo pipefail

APP=/app
PGROOT=/var/lib/postgresql
PGVER=17
PGCLUSTER=main
PGDATA=$PGROOT/$PGVER/$PGCLUSTER
PGPORT=5432
PGSOCK=/var/run/postgresql
DB=webgoatphp
PREFIX=jf_
SCHEMA_REL=install/_db/pdo_pgsql.schema.sql
SCHEMA=$APP/$SCHEMA_REL

echo "== step 02: the schema moves to PostgreSQL 17"

psu() { su -s /bin/sh postgres -c "$*"; }

# ---------------------------------------------------------------- 1. cluster --
# pg_createcluster is the Debian layout: data under /var/lib/postgresql/17/main
# and configuration under /etc/postgresql/17/main. Both directories are carried
# forward to the next step and to grading, so both halves survive. A bare
# `initdb -D /var/lib/postgresql/wg` would also be found -- wg-services and the
# grader look for a PG_VERSION either way -- but then postgresql.conf lives in
# the data directory and there is nothing in /etc/postgresql to carry.
if [ -f "$PGDATA/PG_VERSION" ]; then
    echo "   cluster already present  $PGDATA"
else
    pg_createcluster --locale=C.UTF-8 -e UTF8 -p "$PGPORT" \
        "$PGVER" "$PGCLUSTER" >/dev/null
    echo "   created cluster  $PGDATA"
fi

# The socket directory and the port are what the grader starts the cluster on,
# so set them in the cluster's own configuration too: a deployment that only
# works when someone passes -c on the command line is not a deployment.
CONF=/etc/postgresql/$PGVER/$PGCLUSTER/postgresql.conf
[ -f "$CONF" ] || CONF=$PGDATA/postgresql.conf
python3 - "$CONF" "$PGPORT" "$PGSOCK" <<'PY'
import io, re, sys

conf, port, sock = sys.argv[1:4]
want = [("listen_addresses", "'127.0.0.1'"),
        ("port", port),
        ("unix_socket_directories", "'%s'" % sock)]

with io.open(conf, "r", encoding="utf-8", errors="replace") as fh:
    text = fh.read()

# Drop every existing setting of these three, commented or not, then append
# ours. postgresql.conf takes the last assignment, so appending is the only
# edit a line further down the file cannot override.
for key, _ in want:
    text = re.sub(r"(?m)^[ \t]*#?[ \t]*%s[ \t]*=.*\n" % re.escape(key),
                  "", text)
if not text.endswith("\n"):
    text += "\n"
text += "\n# This deployment's fixed endpoint.\n"
text += "".join("%s = %s\n" % kv for kv in want)

with io.open(conf, "w", encoding="utf-8") as fh:
    fh.write(text)
print("   pinned port/socket/listen_addresses in %s" % conf)
PY

/usr/local/sbin/wg-services start
for i in $(seq 1 60); do
    psu "psql -p $PGPORT -h $PGSOCK -d postgres -Atqc 'SELECT 1'" >/dev/null 2>&1 && break
    sleep 1
done
psu "psql -p $PGPORT -h $PGSOCK -d postgres -Atqc 'SELECT version()'"

# --------------------------------------------------------------- 2. database --
# TEMPLATE template0 so nothing a previous exercise left in template1 comes
# along. The encoding is UTF8 because that is where the data is going; the
# per-column collations the schema file declares are what the case probes read,
# so the database's own locale is deliberately not load-bearing.
if psu "psql -p $PGPORT -h $PGSOCK -d postgres -Atqc \
        \"SELECT 1 FROM pg_database WHERE datname='$DB'\"" | grep -q 1; then
    echo "   database already present  $DB"
else
    psu "psql -p $PGPORT -h $PGSOCK -d postgres -v ON_ERROR_STOP=1 -q -c \
        \"CREATE DATABASE $DB TEMPLATE template0 ENCODING 'UTF8' \
          LC_COLLATE 'C.UTF-8' LC_CTYPE 'C.UTF-8'\""
    echo "   created database  $DB"
fi

# ------------------------------------------------------------ 3. schema file --
install -d -m 755 "$(dirname "$SCHEMA")"
cat > "$SCHEMA" <<'PGSQL'
-- PostgreSQL 17 schema for WebGoatPHP / jFramework.
--
-- Installed by jf\BaseDatabase::InitializeSchema() through GetSQL(),
-- which substitutes PREFIX_ with the configured table prefix. The
-- foreign keys are added after every table exists, because PostgreSQL
-- has no equivalent of the FOREIGN_KEY_CHECKS=0 comment the MySQL
-- dump relies on.

-- Case-insensitive comparison for the columns MySQL gave a _ci
-- collation. Nondeterministic, so equality ignores case the way MySQL's
-- did, and declared here rather than inherited from the database, so the
-- schema means the same thing wherever it is installed.
--
-- The two are not interchangeable: Swedish sorts a-umlaut as its own
-- letter, so latin1_swedish_ci held 'a' and 'a-umlaut' apart while
-- utf8_unicode_ci did not. Asked directly, MySQL 5.7.44 answers 1/1/0
-- and 1/1/1 for case / acute / umlaut; these two locales answer the same.
-- utf8_unicode_ci
CREATE COLLATION IF NOT EXISTS wg_utf8_unicode_ci (provider = icu,
    locale = 'und-u-ks-level1', deterministic = false);
-- latin1_swedish_ci
CREATE COLLATION IF NOT EXISTS wg_latin1_swedish_ci (provider = icu,
    locale = 'sv-u-ks-level1', deterministic = false);


-- app_user
CREATE TABLE app_user (
    firstname character varying(255) COLLATE "wg_utf8_unicode_ci" NOT NULL,
    lastname character varying(255) COLLATE "wg_utf8_unicode_ci" NOT NULL,
    id integer NOT NULL,
    PRIMARY KEY (id)
);

-- contest_challenges
CREATE TABLE contest_challenges (
    id integer GENERATED BY DEFAULT AS IDENTITY NOT NULL,
    contestid integer NOT NULL,
    challengename character varying(100) COLLATE "wg_latin1_swedish_ci" NOT NULL,
    nametodisplay character varying(100) COLLATE "wg_latin1_swedish_ci" NOT NULL,
    points integer NOT NULL,
    totalattempts integer DEFAULT 0,
    completedcount integer DEFAULT 0,
    correctflag character varying(100) COLLATE "wg_latin1_swedish_ci" NOT NULL,
    PRIMARY KEY (id)
);
CREATE UNIQUE INDEX contest_challenges_challengename_idx ON contest_challenges (challengename);
CREATE INDEX contest_challenges_contestid_idx ON contest_challenges (contestid);

-- contest_details
CREATE TABLE contest_details (
    id integer GENERATED BY DEFAULT AS IDENTITY NOT NULL,
    contestname character varying(100) COLLATE "wg_latin1_swedish_ci" NOT NULL,
    contestadmin character varying(100) COLLATE "wg_latin1_swedish_ci",
    starttimestamp integer NOT NULL,
    endtimestamp integer NOT NULL,
    prize character varying(50) COLLATE "wg_latin1_swedish_ci",
    winnerid integer,
    PRIMARY KEY (id)
);
CREATE INDEX contest_details_winnerid_idx ON contest_details (winnerid);

-- contest_submissions
CREATE TABLE contest_submissions (
    id integer GENERATED BY DEFAULT AS IDENTITY NOT NULL,
    userid integer NOT NULL,
    challengeid integer NOT NULL,
    flag character varying(100) COLLATE "wg_latin1_swedish_ci" NOT NULL,
    ip character(15) COLLATE "wg_latin1_swedish_ci",
    timestamp integer NOT NULL,
    PRIMARY KEY (id)
);
CREATE INDEX contest_submissions_challengeid_idx ON contest_submissions (challengeid);
CREATE INDEX contest_submissions_userid_idx ON contest_submissions (userid);

-- jf__test_logs
CREATE TABLE PREFIX__test_logs (
    id integer GENERATED BY DEFAULT AS IDENTITY NOT NULL,
    subject character varying(256) COLLATE "C" NOT NULL,
    data text COLLATE "C" NOT NULL,
    severity integer NOT NULL,
    userid integer,
    sessionid character varying(64) COLLATE "C",
    timestamp integer NOT NULL,
    PRIMARY KEY (id)
);

-- jf__test_options
CREATE TABLE PREFIX__test_options (
    userid integer NOT NULL,
    name character varying(200) COLLATE "C" NOT NULL,
    value text COLLATE "C" NOT NULL,
    expiration integer NOT NULL,
    PRIMARY KEY (userid, name)
);
CREATE INDEX jf__test_options_expiration_idx ON PREFIX__test_options (expiration);

-- jf__test_rbac_permissions
CREATE TABLE PREFIX__test_rbac_permissions (
    id integer GENERATED BY DEFAULT AS IDENTITY NOT NULL,
    lft integer NOT NULL,
    rght integer NOT NULL,
    title character(64) COLLATE "C" NOT NULL,
    description text COLLATE "C" NOT NULL,
    PRIMARY KEY (id)
);
CREATE INDEX jf__test_rbac_permissions_lft_idx ON PREFIX__test_rbac_permissions (lft);
CREATE INDEX jf__test_rbac_permissions_rght_idx ON PREFIX__test_rbac_permissions (rght);
CREATE INDEX jf__test_rbac_permissions_title_idx ON PREFIX__test_rbac_permissions (title);

-- jf__test_rbac_rolepermissions
CREATE TABLE PREFIX__test_rbac_rolepermissions (
    roleid integer NOT NULL,
    permissionid integer NOT NULL,
    assignmentdate integer NOT NULL,
    PRIMARY KEY (roleid, permissionid)
);

-- jf__test_rbac_roles
CREATE TABLE PREFIX__test_rbac_roles (
    id integer GENERATED BY DEFAULT AS IDENTITY NOT NULL,
    lft integer NOT NULL,
    rght integer NOT NULL,
    title character varying(128) COLLATE "C" NOT NULL,
    description text COLLATE "C" NOT NULL,
    PRIMARY KEY (id)
);
CREATE INDEX jf__test_rbac_roles_lft_idx ON PREFIX__test_rbac_roles (lft);
CREATE INDEX jf__test_rbac_roles_rght_idx ON PREFIX__test_rbac_roles (rght);
CREATE INDEX jf__test_rbac_roles_title_idx ON PREFIX__test_rbac_roles (title);

-- jf__test_rbac_userroles
CREATE TABLE PREFIX__test_rbac_userroles (
    userid integer NOT NULL,
    roleid integer NOT NULL,
    assignmentdate integer NOT NULL,
    PRIMARY KEY (userid, roleid)
);

-- jf__test_session  (MySQL engine: MEMORY)
CREATE TABLE PREFIX__test_session (
    id integer GENERATED BY DEFAULT AS IDENTITY NOT NULL,
    sessionid character(64) COLLATE "C" NOT NULL,
    userid integer NOT NULL,
    ip character(15) COLLATE "C" NOT NULL,
    logindate integer NOT NULL,
    lastaccess integer NOT NULL,
    accesscount integer NOT NULL DEFAULT 1,
    currentrequest character varying(1024) COLLATE "C" NOT NULL,
    PRIMARY KEY (id)
);
CREATE UNIQUE INDEX jf__test_session_sessionid_idx ON PREFIX__test_session (sessionid);
CREATE INDEX jf__test_session_userid_idx ON PREFIX__test_session (userid);

-- jf__test_users
CREATE TABLE PREFIX__test_users (
    id integer GENERATED BY DEFAULT AS IDENTITY NOT NULL,
    username character(128) COLLATE "C" NOT NULL,
    password character(128) COLLATE "C" NOT NULL,
    salt character varying(128) COLLATE "C" NOT NULL,
    protocol real NOT NULL,
    PRIMARY KEY (id)
);
CREATE UNIQUE INDEX jf__test_users_username_idx ON PREFIX__test_users (username);

-- jf__test_xuser
CREATE TABLE PREFIX__test_xuser (
    id integer NOT NULL,
    email character varying(256) COLLATE "C" NOT NULL,
    passwordchangetimestamp integer NOT NULL DEFAULT 0,
    temporaryresetpassword character varying(256) COLLATE "C" NOT NULL DEFAULT '',
    temporaryresetpasswordtimeout integer NOT NULL DEFAULT 0,
    lastlogintimestamp integer NOT NULL DEFAULT 0,
    failedloginattempts integer NOT NULL DEFAULT 0,
    locktimeout integer NOT NULL DEFAULT 0,
    activated integer NOT NULL DEFAULT 0,
    createtimestamp integer NOT NULL,
    PRIMARY KEY (id)
);
CREATE UNIQUE INDEX jf__test_xuser_email_idx ON PREFIX__test_xuser (left(email, 128));

-- jf_logs
CREATE TABLE PREFIX_logs (
    id integer GENERATED BY DEFAULT AS IDENTITY NOT NULL,
    subject character varying(256) COLLATE "C" NOT NULL,
    data text COLLATE "C" NOT NULL,
    severity integer NOT NULL,
    userid integer,
    sessionid character varying(64) COLLATE "C",
    timestamp integer NOT NULL,
    PRIMARY KEY (id)
);

-- jf_options
CREATE TABLE PREFIX_options (
    userid integer NOT NULL,
    name character varying(200) COLLATE "C" NOT NULL,
    value text COLLATE "C" NOT NULL,
    expiration integer NOT NULL,
    PRIMARY KEY (userid, name)
);
CREATE INDEX jf_options_expiration_idx ON PREFIX_options (expiration);

-- jf_rbac_permissions
CREATE TABLE PREFIX_rbac_permissions (
    id integer GENERATED BY DEFAULT AS IDENTITY NOT NULL,
    lft integer NOT NULL,
    rght integer NOT NULL,
    title character(64) COLLATE "C" NOT NULL,
    description text COLLATE "C" NOT NULL,
    PRIMARY KEY (id)
);
CREATE INDEX jf_rbac_permissions_lft_idx ON PREFIX_rbac_permissions (lft);
CREATE INDEX jf_rbac_permissions_rght_idx ON PREFIX_rbac_permissions (rght);
CREATE INDEX jf_rbac_permissions_title_idx ON PREFIX_rbac_permissions (title);

-- jf_rbac_rolepermissions
CREATE TABLE PREFIX_rbac_rolepermissions (
    roleid integer NOT NULL,
    permissionid integer NOT NULL,
    assignmentdate integer NOT NULL,
    PRIMARY KEY (roleid, permissionid)
);

-- jf_rbac_roles
CREATE TABLE PREFIX_rbac_roles (
    id integer GENERATED BY DEFAULT AS IDENTITY NOT NULL,
    lft integer NOT NULL,
    rght integer NOT NULL,
    title character varying(128) COLLATE "C" NOT NULL,
    description text COLLATE "C" NOT NULL,
    PRIMARY KEY (id)
);
CREATE INDEX jf_rbac_roles_lft_idx ON PREFIX_rbac_roles (lft);
CREATE INDEX jf_rbac_roles_rght_idx ON PREFIX_rbac_roles (rght);
CREATE INDEX jf_rbac_roles_title_idx ON PREFIX_rbac_roles (title);

-- jf_rbac_userroles
CREATE TABLE PREFIX_rbac_userroles (
    userid integer NOT NULL,
    roleid integer NOT NULL,
    assignmentdate integer NOT NULL,
    PRIMARY KEY (userid, roleid)
);

-- jf_session  (MySQL engine: MEMORY)
CREATE TABLE PREFIX_session (
    id integer GENERATED BY DEFAULT AS IDENTITY NOT NULL,
    sessionid character(64) COLLATE "C" NOT NULL,
    userid integer NOT NULL,
    ip character(15) COLLATE "C" NOT NULL,
    logindate integer NOT NULL,
    lastaccess integer NOT NULL,
    accesscount integer NOT NULL DEFAULT 1,
    currentrequest character varying(1024) COLLATE "C" NOT NULL,
    PRIMARY KEY (id)
);
CREATE UNIQUE INDEX jf_session_sessionid_idx ON PREFIX_session (sessionid);
CREATE INDEX jf_session_userid_idx ON PREFIX_session (userid);

-- jf_stats
CREATE TABLE PREFIX_stats (
    id integer GENERATED BY DEFAULT AS IDENTITY NOT NULL,
    userid integer NOT NULL,
    sessionid character varying(128) COLLATE "C" NOT NULL,
    timestamp integer NOT NULL,
    page character varying(1024) COLLATE "C" NOT NULL,
    query character varying(1024) COLLATE "C" NOT NULL,
    ip character(15) COLLATE "C" NOT NULL,
    host character varying(256) COLLATE "C" NOT NULL,
    protocol character varying(10) COLLATE "C" NOT NULL,
    useragent character varying(1024) COLLATE "C" NOT NULL,
    PRIMARY KEY (id)
);
CREATE INDEX jf_stats_host_idx ON PREFIX_stats (left(host, 255));
CREATE INDEX jf_stats_ip_idx ON PREFIX_stats (ip);
CREATE INDEX jf_stats_page_idx ON PREFIX_stats (left(page, 255));
CREATE INDEX jf_stats_protocol_idx ON PREFIX_stats (protocol);
CREATE INDEX jf_stats_sessionid_idx ON PREFIX_stats (sessionid);
CREATE INDEX jf_stats_timestamp_idx ON PREFIX_stats (timestamp);
CREATE INDEX jf_stats_userid_idx ON PREFIX_stats (userid);

-- jf_users
CREATE TABLE PREFIX_users (
    id integer GENERATED BY DEFAULT AS IDENTITY NOT NULL,
    username character varying(255) COLLATE "C" NOT NULL,
    password character varying(255) COLLATE "C" NOT NULL,
    salt character varying(255) COLLATE "C" NOT NULL,
    protocol real NOT NULL,
    discriminator character varying(255) COLLATE "C" NOT NULL,
    PRIMARY KEY (id)
);
CREATE UNIQUE INDEX jf_users_username_idx ON PREFIX_users (username);

-- jf_xuser
CREATE TABLE PREFIX_xuser (
    id integer NOT NULL,
    email character varying(255) COLLATE "C" NOT NULL,
    passwordchangetimestamp integer NOT NULL,
    temporaryresetpassword character varying(255) COLLATE "C" NOT NULL,
    temporaryresetpasswordtimeout integer NOT NULL,
    lastlogintimestamp integer NOT NULL,
    failedloginattempts integer NOT NULL,
    locktimeout integer NOT NULL,
    activated smallint NOT NULL,
    createtimestamp integer NOT NULL,
    CONSTRAINT jf_xuser_activated_range CHECK (activated BETWEEN -128 AND 127),
    PRIMARY KEY (id)
);

-- lesson_AccessControlMatrix_users
CREATE TABLE lesson_accesscontrolmatrix_users (
    user_id integer NOT NULL,
    name character varying(30) COLLATE "wg_latin1_swedish_ci"
);
CREATE UNIQUE INDEX lesson_accesscontrolmatrix_users_name_idx ON lesson_accesscontrolmatrix_users (name);

-- lesson_BusinessLayerAccessControl_users
CREATE TABLE lesson_businesslayeraccesscontrol_users (
    id integer GENERATED BY DEFAULT AS IDENTITY NOT NULL,
    role character varying(30) COLLATE "wg_latin1_swedish_ci" NOT NULL,
    first_name character varying(50) COLLATE "wg_latin1_swedish_ci",
    last_name character varying(50) COLLATE "wg_latin1_swedish_ci",
    password character varying(50) COLLATE "wg_latin1_swedish_ci",
    street character varying(100) COLLATE "wg_latin1_swedish_ci",
    city character varying(50) COLLATE "wg_latin1_swedish_ci",
    phone character varying(20) COLLATE "wg_latin1_swedish_ci",
    salary integer,
    cc_no character varying(20) COLLATE "wg_latin1_swedish_ci",
    cc_limit character varying(10) COLLATE "wg_latin1_swedish_ci",
    PRIMARY KEY (id)
);

-- lesson_NumericSQLInjection_weather
CREATE TABLE lesson_numericsqlinjection_weather (
    station integer GENERATED BY DEFAULT AS IDENTITY NOT NULL,
    name character varying(30) COLLATE "wg_latin1_swedish_ci" NOT NULL,
    state character varying(5) COLLATE "wg_latin1_swedish_ci" NOT NULL,
    min_temp integer NOT NULL,
    max_temp integer NOT NULL,
    PRIMARY KEY (station)
);

-- lesson_XSS2_messages
CREATE TABLE lesson_xss2_messages (
    id integer GENERATED BY DEFAULT AS IDENTITY NOT NULL,
    message text COLLATE "wg_latin1_swedish_ci",
    PRIMARY KEY (id)
);

-- Foreign keys, once every table they name exists.
ALTER TABLE app_user ADD CONSTRAINT fk_88bdf3e911d3633a
    FOREIGN KEY (id) REFERENCES PREFIX_users (id);
ALTER TABLE contest_challenges ADD CONSTRAINT contest_challenges_ibfk_1
    FOREIGN KEY (contestid) REFERENCES contest_details (id);
ALTER TABLE contest_details ADD CONSTRAINT contest_details_ibfk_1
    FOREIGN KEY (winnerid) REFERENCES PREFIX_users (id);
ALTER TABLE contest_submissions ADD CONSTRAINT contest_submissions_ibfk_1
    FOREIGN KEY (userid) REFERENCES PREFIX_users (id);
ALTER TABLE contest_submissions ADD CONSTRAINT contest_submissions_ibfk_2
    FOREIGN KEY (challengeid) REFERENCES contest_challenges (id);
ALTER TABLE PREFIX_xuser ADD CONSTRAINT fk_54c202bc11d3633a
    FOREIGN KEY (id) REFERENCES PREFIX_users (id);
PGSQL
echo "   wrote $SCHEMA ($(wc -c < "$SCHEMA") bytes)"

# ------------------------------------------------------------- 4. install it --
# The way the framework does it: GetSQL() substitutes PREFIX_ with the
# configured table prefix and BaseDatabase::InitializeSchema() runs what comes
# back statement by statement, ignoring failures. Here the failures are not
# ignored -- ON_ERROR_STOP=1 -- precisely because the framework would not have
# told us. A file that needs that flag off is a file that is still broken.
RUNSQL=$(mktemp /tmp/pdo_pgsql.installed.XXXXXX.sql)
trap 'rm -f "$RUNSQL"' EXIT
sed "s/PREFIX_/$PREFIX/g" "$SCHEMA" > "$RUNSQL"
chmod 644 "$RUNSQL"
psu "psql -p $PGPORT -h $PGSOCK -d $DB -v ON_ERROR_STOP=1 -q -f '$RUNSQL'"
echo "   installed the schema into $DB"

# ------------------------------------------------------------------ 5. check --
# Assert the outcome rather than the exit status: a migration script that
# silently did nothing is the failure mode worth ten lines.
COUNT=$(psu "psql -p $PGPORT -h $PGSOCK -d $DB -Atqc \
    \"SELECT count(*) FROM pg_class c JOIN pg_namespace n ON n.oid=c.relnamespace \
      WHERE c.relkind='r' AND n.nspname NOT IN ('pg_catalog','information_schema')\"")
FKS=$(psu "psql -p $PGPORT -h $PGSOCK -d $DB -Atqc \
    \"SELECT count(*) FROM pg_constraint WHERE contype='f'\"")
IDENT=$(psu "psql -p $PGPORT -h $PGSOCK -d $DB -Atqc \
    \"SELECT count(*) FROM pg_attribute WHERE attidentity <> '' AND attnum > 0\"")
echo "   tables=$COUNT foreign-keys=$FKS identity-columns=$IDENT"
[ "$COUNT" = 27 ] || { echo "expected 27 tables, found $COUNT" >&2; exit 1; }
[ "$FKS" = 6 ] || { echo "expected 6 foreign keys, found $FKS" >&2; exit 1; }
[ "$IDENT" = 17 ] || { echo "expected 17 identity columns, found $IDENT" >&2; exit 1; }

# The mixed-case names the application uses must each resolve to exactly one
# relation, and none may differ from another only in case.
DUPES=$(psu "psql -p $PGPORT -h $PGSOCK -d $DB -Atqc \
    \"SELECT count(*) FROM (SELECT lower(c.relname) FROM pg_class c \
        JOIN pg_namespace n ON n.oid=c.relnamespace \
        WHERE c.relkind IN ('r','v','m') \
          AND n.nspname NOT IN ('pg_catalog','information_schema') \
        GROUP BY 1 HAVING count(*) > 1) d\"")
[ "$DUPES" = 0 ] || { echo "$DUPES name(s) differ only in case" >&2; exit 1; }

# Step 1's deliverables are still in place. Cheap to assert, and the step is
# scored on them as guards.
grep -q 'STRICT_TRANS_TABLES' /etc/mysql/my.cnf \
    || { echo "step 1's sql_mode is gone from /etc/mysql/my.cnf" >&2; exit 1; }

/usr/local/sbin/wg-services status || true
echo "== step 02 done"
