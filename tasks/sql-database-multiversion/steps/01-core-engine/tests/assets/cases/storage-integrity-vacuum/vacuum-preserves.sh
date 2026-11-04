set -u
S="$SQLITE3"
$S -init /dev/null -batch main.db "CREATE TABLE t(a INTEGER PRIMARY KEY, b TEXT); CREATE INDEX i ON t(b);" || exit 1
$S -init /dev/null -batch main.db "INSERT INTO t SELECT NULL, 'row' FROM (SELECT 1 UNION ALL SELECT 2 UNION ALL SELECT 3);"
$S -init /dev/null -batch main.db "INSERT INTO t VALUES(100, 'kept');"
$S -init /dev/null -batch main.db "DELETE FROM t WHERE a < 3;"
$S -init /dev/null -batch main.db "PRAGMA integrity_check;"
$S -init /dev/null -batch main.db "VACUUM;" || echo "vacuum failed"
$S -init /dev/null -batch main.db "SELECT a, b FROM t ORDER BY a;"
$S -init /dev/null -batch main.db "SELECT type, name FROM sqlite_master ORDER BY name;"
$S -init /dev/null -batch main.db "PRAGMA integrity_check;"
