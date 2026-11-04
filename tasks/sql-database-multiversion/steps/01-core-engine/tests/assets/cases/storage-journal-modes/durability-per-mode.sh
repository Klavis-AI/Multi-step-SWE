set -u
S="$SQLITE3"
$S -init /dev/null -batch main.db "CREATE TABLE t(a INTEGER PRIMARY KEY, m TEXT);" || exit 1
n=0
for m in delete truncate persist memory; do
    n=$(( n + 1 ))
    $S -init /dev/null -batch main.db "PRAGMA journal_mode = $m; INSERT INTO t VALUES($n, '$m');" > /dev/null
    # rollback under this mode leaves no trace
    $S -init /dev/null -batch main.db "PRAGMA journal_mode = $m; BEGIN; INSERT INTO t VALUES(100 + $n, 'gone'); ROLLBACK;" > /dev/null
done
$S -init /dev/null -batch main.db "SELECT a, m FROM t ORDER BY a;"
$S -init /dev/null -batch main.db "PRAGMA integrity_check;"
