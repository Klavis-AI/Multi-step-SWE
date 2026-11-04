set -u
S="$SQLITE3"
$S -init /dev/null -batch main.db "CREATE TABLE t(a INTEGER PRIMARY KEY, b TEXT); INSERT INTO t VALUES(1,'p'),(2,'q');" || exit 1
# a second process, started after the first exited, sees the committed rows
$S -init /dev/null -batch main.db "SELECT a, b FROM t ORDER BY a;"
# and can add to them
$S -init /dev/null -batch main.db "INSERT INTO t VALUES(3,'r');"
$S -init /dev/null -batch main.db "SELECT count(*) FROM t;"
# an explicit transaction that commits survives too
$S -init /dev/null -batch main.db "BEGIN; INSERT INTO t VALUES(4,'s'); COMMIT;"
$S -init /dev/null -batch main.db "SELECT a, b FROM t ORDER BY a;"
