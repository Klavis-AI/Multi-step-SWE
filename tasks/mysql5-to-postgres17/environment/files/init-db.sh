#!/bin/bash
# Build-time initialisation of the MySQL 5.7 server this deployment runs on.
# Creates the data directory, the application's database and role, imports
# SQL/webgoat.sql exactly as README.md says to, and then loads the deployment's
# own data from /tmp/seed (applied in filename order).
set -euo pipefail

export LD_LIBRARY_PATH=/opt/mysql57/lib
SOCK=/var/run/mysqld/mysqld.sock
MYSQL="/opt/mysql57/bin/mysql --socket=$SOCK -uroot"

DB_NAME=webgoatphp
DB_USER=webgoat
DB_PASS=webgoat

mkdir -p /var/lib/mysql /var/run/mysqld /var/lib/mysql-files /var/log/mysql
chown -R mysql:mysql /var/lib/mysql /var/run/mysqld /var/lib/mysql-files /var/log/mysql
chmod 750 /var/lib/mysql-files

echo "== initialising the MySQL data directory =="
/opt/mysql57/bin/mysqld --defaults-file=/etc/mysql/my.cnf --initialize-insecure

echo "== starting MySQL =="
/etc/init.d/mysql start

echo "== creating the application database and role =="
$MYSQL -e "CREATE DATABASE \`$DB_NAME\` DEFAULT CHARACTER SET utf8 COLLATE utf8_bin;"
$MYSQL -e "CREATE USER '$DB_USER'@'localhost' IDENTIFIED BY '$DB_PASS';"
$MYSQL -e "CREATE USER '$DB_USER'@'127.0.0.1' IDENTIFIED BY '$DB_PASS';"
$MYSQL -e "GRANT ALL PRIVILEGES ON \`$DB_NAME\`.* TO '$DB_USER'@'localhost';"
$MYSQL -e "GRANT ALL PRIVILEGES ON \`$DB_NAME\`.* TO '$DB_USER'@'127.0.0.1';"
$MYSQL -e "FLUSH PRIVILEGES;"

echo "== importing SQL/webgoat.sql =="
$MYSQL "$DB_NAME" < /app/SQL/webgoat.sql

tables=$($MYSQL -N -B -e "SELECT COUNT(*) FROM information_schema.tables WHERE table_schema='$DB_NAME';")
echo "tables after SQL/webgoat.sql: $tables"
test "$tables" -ge 27

if compgen -G '/tmp/seed/*.sql' >/dev/null; then
	for f in /tmp/seed/*.sql; do
		echo "== loading seed $(basename "$f") =="
		$MYSQL "$DB_NAME" < "$f"
	done
fi

echo "== stopping MySQL cleanly =="
/etc/init.d/mysql stop

echo "== init-db.sh done =="
