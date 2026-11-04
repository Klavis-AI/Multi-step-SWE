#!/usr/bin/env bash
#
# Reference solution for step 05 -- "MySQL is gone".
#
# Step 04 moved the application onto PostgreSQL: every page is served from it,
# and no request reads MySQL any more. What step 04 did *not* do is remove the
# old server, and the difference between "nothing uses it" and "it is gone" is
# the whole of this step. The tree it hands over still carries two jframework
# adapters, three adapter self-tests, seven MySQL-dialect installer files,
# thirteen MySQL and Drizzle files in the vendored Doctrine tree, a data
# directory and a configuration directory that travel as artifacts, and a
# provisioning recipe that reinstalls mysql-server and phpMyAdmin on the next
# `vagrant up`. A deployment in that state is one `apt-get` away from having
# two databases again.
#
# Three kinds of work, and only the second and third are scored (§24.2):
#
#   * Deletion. Twenty-five files and two directories. A name-based sweep
#     finds all of them, which is exactly why they are guards -- they must be
#     true, and doing them is not evidence of anything.
#
#   * The wiring. Nineteen string literals, in four files that must *survive*,
#     that still name a MySQL back-end: a registry that advertises drivers
#     whose implementations this step deletes, a test harness that imports
#     adapters that no longer exist, and a Doctrine bootstrap that falls back
#     to pdo_mysql when the configuration does not name an adapter. Each one
#     needs an edit to a file that stays, which is what makes it scorable.
#
#   * The recipe. `vagrant_bootstrap.sh` has to stop installing the server and
#     start installing PostgreSQL, while still producing a machine that runs
#     the application.
#
# Every edit asserts what it expected to find before it makes it (trap 7):
# a `sed` that matches nothing exits 0 and leaves the file alone, which is
# indistinguishable from success unless the count is checked.

set -euo pipefail
cd /app

say() { printf '  %s\n' "$*"; }

# Replace exactly $3 occurrences of the literal $1 with $2 in $4, or fail.
#
# Python and not sed/perl, and everything through argv rather than through an
# interpolated script. The text being matched here is PHP: it holds `$adapter`,
# `"`, `(` and `\`, and every one of those means something to a shell-quoted
# `perl -e` or to a regex. `\Q` does not help -- it quotes metacharacters and
# still interpolates variables, so `s/\Q$pat\E/` on a pattern containing
# `$adapter` searched for the text with `$adapter` deleted, matched nothing,
# and left the file untouched. The count assert is what caught it.
cat > /tmp/edit.py <<'EDIT'
import io, sys

path, pat, repl, want = sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4])
s = io.open(path, encoding="utf-8", errors="surrogateescape").read()
got = s.count(pat)
if got != want:
    sys.exit("%s: expected %d occurrence(s) of %r, found %d"
             % (path, want, pat, got))
s = s.replace(pat, repl)
if pat in s:
    sys.exit("%s: %r survived the edit" % (path, pat))
io.open(path, "w", encoding="utf-8",
        errors="surrogateescape").write(s)
EDIT

edit() { python3 /tmp/edit.py "$4" "$1" "$2" "$3"; }

# ------------------------------------------------- 1. the files that go ---

say "removing the MySQL adapters, their self-tests and their installer SQL"
for f in _japp/model/lib/db/adapter/pdo_mysql.php \
         _japp/model/lib/db/adapter/mariadb.php; do
    [ -e "$f" ] || { echo "solve.sh: $f is already gone" >&2; exit 1; }
done
rm -f _japp/model/lib/db/adapter/pdo_mysql.php \
      _japp/model/lib/db/adapter/mariadb.php \
      _japp/test/lib/db/adapter/mysqli.php \
      _japp/test/lib/db/adapter/pdo_mysql.php \
      _japp/test/lib/db/adapter/mariadb.php
rm -f install/_db/mariadb.data.sql install/_db/mariadb.schema.sql \
      install/_db/mysqli.data.sql install/_db/mysqli.schema.sql \
      install/_db/mysqli.test.schema.sql \
      install/_db/pdo_mysql.data.sql install/_db/pdo_mysql.schema.sql

say "removing Doctrine's MySQL and Drizzle drivers, platforms and schema managers"
# Doctrine is vendored under the plugin, not under vendor/.
D=app/plugin/doctrine/Doctrine/DBAL
[ -d "$D" ] || { echo "solve.sh: no Doctrine tree at $D" >&2; exit 1; }
rm -rf "$D/Driver/Mysqli" "$D/Driver/DrizzlePDOMySql"
rm -f "$D/Event/Listeners/MysqlSessionInit.php" \
      "$D/Platforms/DrizzlePlatform.php" "$D/Platforms/MySqlPlatform.php" \
      "$D/Platforms/Keywords/DrizzleKeywords.php" \
      "$D/Platforms/Keywords/MySQLKeywords.php" \
      "$D/Schema/DrizzleSchemaManager.php" \
      "$D/Schema/MySqlSchemaManager.php"

# The server itself. `wg-services` is written to come up without it.
say "removing the server's data and configuration"
rm -rf /var/lib/mysql /etc/mysql

# --------------------------------------------- 2. the wiring that stays ---

# Doctrine's driver registry and its keyword-list registry. Each names a class
# this step deletes; removing the whole entry is the fix, because a key left
# pointing at a class that is no longer there is precisely the half-finished
# state the step is about.
#
# Keyed on the map key rather than on the whole line. The shipped files pad
# three of these keys to a column -- `'pdo_mysql'  =>` with two spaces, not one
# -- and a literal pattern that assumed one space matched nothing, changed
# nothing, and said so only because the count was asserted.
cat > /tmp/drop_map_keys.py <<'DROP'
import io, re, sys

path, keys = sys.argv[1], sys.argv[2:]
s = io.open(path, encoding="utf-8").read()
for k in keys:
    # The whole entry line: leading indent, the quoted key, `=>` with any
    # padding either side, the quoted class name, the comma, any trailing
    # space, the newline.
    pat = r"^[ \t]*'%s'[ \t]*=>[ \t]*'[^']*',[ \t]*\r?\n" % re.escape(k)
    s, n = re.subn(pat, "", s, flags=re.M)
    if n != 1:
        sys.exit("%s: expected exactly 1 entry for %r, removed %d"
                 % (path, k, n))
io.open(path, "w", encoding="utf-8").write(s)
DROP

drop_map_keys() {
    python3 /tmp/drop_map_keys.py "$@"
}

say "Doctrine DriverManager: dropping the three MySQL/Drizzle driver entries"
drop_map_keys "$D/DriverManager.php" pdo_mysql drizzle_pdo_mysql mysqli

# This vendored Doctrine predates the Drizzle keyword list, so `mysql` is the
# only entry in the registry -- and the command also carries a *default* list
# of keyword sets to check against, which names mysql a second time.
say "Doctrine ReservedWordsCommand: dropping the MySQL keyword list"
F="$D/Tools/Console/Command/ReservedWordsCommand.php"
drop_map_keys "$F" mysql
edit "array('mysql', 'pgsql', 'sqlite', 'oracle', 'mssql')" \
     "array('pgsql', 'sqlite', 'oracle', 'mssql')" 1 "$F"

# The application's own Doctrine bootstrap. This is the sharpest one: the
# fallback fires whenever the configuration does not name an adapter, so a
# tree that deleted every MySQL file and left this alone asks Doctrine for a
# driver that is no longer there.
say "app/plugin/doctrine.php: the fallback adapter is pdo_pgsql, not pdo_mysql"
F=app/plugin/doctrine.php
edit 'if ($adapter == "mysql" or $adapter == "mysqli" or !$adapter)' \
     'if (!$adapter)' 1 "$F"
edit '$adapter = "pdo_mysql";' '$adapter = "pdo_pgsql";' 1 "$F"

# The adapter test harness. One `switch` on the configured adapter, whose first
# arm -- three case labels sharing one body -- imports the three adapter test
# files this step deletes. The second arm is the shipped tree's own bug: the
# `pdo_sqlite` case imports `pdo_mysql`. Deleting the file it names is what
# turns that from a harmless oddity into a fatal one, so this step fixes it to
# import the adapter the arm is actually about.
#
# The whole switch is replaced rather than picked at line by line: removing the
# three case labels alone would leave their body and its `break` stranded ahead
# of the first surviving case.
say "_japp/test/lib/db/adapter/main.php: only the adapters that still exist"
F=_japp/test/lib/db/adapter/main.php
python3 - "$F" <<'PY'
import io, re, sys

p = sys.argv[1]
s = io.open(p, encoding="utf-8", errors="surrogateescape").read()

want = re.findall(r'jf/test/lib/db/adapter/(mysqli|pdo_mysql|mariaDB)', s)
if len(want) != 4:
    sys.exit("main.php: expected 4 MySQL import paths, found %d" % len(want))

body = re.search(r'switch \(\$userConfig->Adapter\)\s*\r?\n\s*\{.*?\n\s*\}\r?\n',
                 s, re.S)
if not body:
    sys.exit("main.php: the switch this edit rewrites is not there")

indent = "\t\t"
new = (indent + 'switch ($userConfig->Adapter)\n'
       + indent + '{\n'
       + indent + '\tcase "pdo_sqlite":\n'
       + indent + '\t\t$this->add("jf/test/lib/db/adapter/pdo_sqlite");\n'
       + indent + '\t\tbreak;\n'
       + indent + '}\n')
s = s[:body.start()] + new.lstrip("\t") + s[body.end():]

left = re.findall(r'jf/test/lib/db/adapter/(mysqli|pdo_mysql|mariaDB)', s)
if left:
    sys.exit("main.php: %d MySQL import path(s) survived" % len(left))
io.open(p, "w", encoding="utf-8", errors="surrogateescape").write(s)
PY
php -l "$F" >/dev/null

# ------------------------------------------------------- 3. the recipe ---

say "vagrant_bootstrap.sh: provision PostgreSQL instead of MySQL"
python3 - vagrant_bootstrap.sh <<'PY'
import io, re, sys
p = sys.argv[1]
s = io.open(p, encoding="utf-8").read()
for must in ("mysql-server", "phpmyadmin", "php5-mysql"):
    if must not in s:
        sys.exit("vagrant_bootstrap.sh: expected to find %r" % must)

# The debconf preseeds exist only to answer mysql-server's and phpmyadmin's
# password prompts, and the password variable exists only to fill them in.
# Nothing here prompts any more.
s = re.sub(r'^.*debconf-set-selections.*\n', '', s, flags=re.M)
s = re.sub(r'^mysql_password=.*\n', '', s, flags=re.M)

# The server itself. phpMyAdmin goes rather than being replaced: it is a MySQL
# administration front-end, it was never part of the application, and this
# deployment has no equivalent to swap it for.
s = re.sub(r'^\s*(sudo\s+)?apt-get install -y mysql-server\s*\n',
           'apt-get install -y postgresql postgresql-client\n', s, flags=re.M)
s = re.sub(r'^\s*(sudo\s+)?apt-get install -y phpmyadmin\s*\n', '', s, flags=re.M)
s = s.replace("php5-mysql", "php5-pgsql")

# The section headers and the commented-out `mysqladmin` line the two sections
# left behind. The scorer tokenises with `#` as a comment character and so
# would not have seen these; a recipe that still says "#set up mysql" over a
# block that sets up PostgreSQL is a thing to fix anyway.
s = re.sub(r'^#\s*set[- ]?up (mysql|phpmyadmin)\s*\n', '', s,
           flags=re.M | re.I)
s = re.sub(r'^#\s*mysqladmin.*\n', '', s, flags=re.M)
s = re.sub(r'\n{3,}', '\n\n', s)

for gone in ("mysql-server", "phpmyadmin", "php5-mysql", "root_password"):
    if gone in s:
        sys.exit("vagrant_bootstrap.sh: %r survived" % gone)
for need in ("postgresql", "php5-pgsql", "apache2"):
    if need not in s:
        sys.exit("vagrant_bootstrap.sh: %r is missing" % need)
io.open(p, "w", encoding="utf-8").write(s)
PY
bash -n vagrant_bootstrap.sh

say "done"
