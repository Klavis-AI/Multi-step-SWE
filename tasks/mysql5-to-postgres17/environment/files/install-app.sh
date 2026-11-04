#!/bin/bash
# Install WebGoatPHP the way its own README.md describes:
#
#   "Move it to your document root
#    Import the database from SQL/webgoat.sql
#    Enter your database connection details in app/config/application.php"
#
# The connection details are written by the application's own installer,
# app/config/setup.php, so this script runs exactly that substitution rather
# than editing the file by hand.
set -euo pipefail

SRC=/tmp/webgoat-src.tar.gz
DEST=/app

DB_NAME=webgoatphp
DB_USER=webgoat
DB_PASS=webgoat
DEVELOP_URL=localhost

echo "== unpacking the pinned WebGoatPHP tree =="
mkdir -p "$DEST"
tar -xzf "$SRC" -C /tmp
shopt -s dotglob
mv /tmp/webgoat/* "$DEST"/
shopt -u dotglob
rmdir /tmp/webgoat
rm -f "$SRC"
test -f "$DEST/.htaccess"
test -f "$DEST/SQL/webgoat.sql"
test -f "$DEST/app/config/application.php"

echo "== running app/config/setup.php's substitution =="
DB_USER="$DB_USER" DB_PASS="$DB_PASS" DB_NAME="$DB_NAME" DEVELOP_URL="$DEVELOP_URL" \
php -r '
$config = array(
    "DBUSER"       => getenv("DB_USER"),
    "DBPASS"       => getenv("DB_PASS"),
    "DBNAME"       => getenv("DB_NAME"),
    "LOCALHOSTURL" => getenv("DEVELOP_URL"),
);
$configFile = "/app/app/config/application.php";
$configData = file_get_contents($configFile);
$before = $configData;
$configData = str_replace(array_keys($config), $config, $configData);
$configData = preg_replace("/jf::import.*?INITIALSETUP\n/", "", $configData);
if ($configData === $before) {
    fwrite(STDERR, "setup substitution changed nothing\n");
    exit(1);
}
file_put_contents($configFile, $configData);
'

# Assert the substitution landed: no placeholder may survive, and the connection
# line must name this deployment's database.
! grep -qE 'DBNAME|DBUSER|DBPASS|LOCALHOSTURL' "$DEST/app/config/application.php"
grep -q "DatabaseSetting(\"mysqli\", \"$DB_NAME\", \"$DB_USER\", \"$DB_PASS\")" "$DEST/app/config/application.php"
! grep -q 'INITIALSETUP' "$DEST/app/config/application.php"

echo "== disabling jframework's error-to-exception handler =="
# jframework's ErrorHandler converts every E_WARNING/E_NOTICE into an
# ErrorException, which aborts the request. On PHP 7.4 the vendored Doctrine ORM
# emits a compile-time E_WARNING ('"continue" targeting switch is equivalent to
# "break"', Doctrine/ORM/UnitOfWork.php:2534), so with the handler on, every
# single request dies. application.php already owns this switch; this deployment
# turns it off and relies on php.ini's display_errors=Off / log_errors=On, which
# is how the application behaved on the PHP 5.x it was written for.
python3 - <<'PY'
import io, sys
p = "/app/app/config/application.php"
s = io.open(p, encoding="utf-8").read()
old = "    jf\\ErrorHandler::$Enabled=true; //Enables jframework's built-in error handler"
new = "    jf\\ErrorHandler::$Enabled=false; //see php.ini display_errors/log_errors"
if old not in s:
    sys.exit("error-handler anchor not found in application.php")
s = s.replace(old, new)
io.open(p, "w", encoding="utf-8").write(s)
PY
test "$(grep -c 'ErrorHandler::\$Enabled=false' "$DEST/app/config/application.php")" = 2

echo "== filesystem permissions =="
mkdir -p "$DEST/app/plugin/doctrine/proxy_cache"
chown -R www-data:www-data "$DEST"
chmod 1777 "$DEST/app/plugin/doctrine/proxy_cache"

echo "== install-app.sh done =="
