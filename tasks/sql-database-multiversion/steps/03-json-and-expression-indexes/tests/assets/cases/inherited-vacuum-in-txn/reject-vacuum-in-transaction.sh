set -u
S="$SQLITE3"
$S -init /dev/null -batch main.db "CREATE TABLE t(a INTEGER PRIMARY KEY); INSERT INTO t VALUES(1);" || exit 1
if $S -init /dev/null -batch main.db "BEGIN; VACUUM;" 2> /dev/null; then
    echo "vacuum inside a transaction was accepted"
else
    echo "rejected"
fi
$S -init /dev/null -batch main.db "SELECT a FROM t ORDER BY a;"
$S -init /dev/null -batch main.db "PRAGMA integrity_check;"
