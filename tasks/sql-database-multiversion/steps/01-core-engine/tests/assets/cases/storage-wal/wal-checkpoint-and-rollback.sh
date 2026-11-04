set -u
S="$SQLITE3"
$S -init /dev/null -batch main.db "CREATE TABLE t(a INTEGER PRIMARY KEY);" || exit 1
$S -init /dev/null -batch main.db "PRAGMA journal_mode = wal;" > /dev/null
$S -init /dev/null -batch main.db "INSERT INTO t VALUES(1); INSERT INTO t VALUES(2);"
$S -init /dev/null -batch main.db "PRAGMA wal_checkpoint;" > /dev/null || echo "checkpoint failed"
$S -init /dev/null -batch main.db "SELECT a FROM t ORDER BY a;"
# rollback still works in WAL mode
$S -init /dev/null -batch main.db "BEGIN; INSERT INTO t VALUES(3); ROLLBACK;"
$S -init /dev/null -batch main.db "SELECT a FROM t ORDER BY a;"
$S -init /dev/null -batch main.db "PRAGMA integrity_check;"
$S -init /dev/null -batch main.db "PRAGMA journal_mode;"
