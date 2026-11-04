set -u
S="$SQLITE3"
$S -init /dev/null -batch main.db "CREATE TABLE t(a INTEGER PRIMARY KEY); INSERT INTO t VALUES(1);" || exit 1
$S -init /dev/null -batch main.db "BEGIN; INSERT INTO t VALUES(2); INSERT INTO t VALUES(3); ROLLBACK;"
$S -init /dev/null -batch main.db "SELECT a FROM t ORDER BY a;"
# a transaction left open when the process exits is not committed
$S -init /dev/null -batch main.db "BEGIN; INSERT INTO t VALUES(4);"
$S -init /dev/null -batch main.db "SELECT a FROM t ORDER BY a;"
$S -init /dev/null -batch main.db "PRAGMA integrity_check;"
