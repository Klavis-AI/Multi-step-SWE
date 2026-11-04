set -u
S="$SQLITE3"
vers() { od -An -tu1 -j18 -N2 -- main.db | tr -s ' ' | sed -e 's/^ *//' -e 's/ *$//'; }

$S -init /dev/null -batch main.db "CREATE TABLE t(a INTEGER PRIMARY KEY, b TEXT); INSERT INTO t VALUES(1,'p');" || exit 1
echo "rollback_versions=$(vers)"
echo "set=$($S -init /dev/null -batch main.db 'PRAGMA journal_mode = wal;')"
echo "wal_versions=$(vers)"
# the mode is a property of the file, so a new connection reports it too
echo "reread=$($S -init /dev/null -batch main.db 'PRAGMA journal_mode;')"
$S -init /dev/null -batch main.db "INSERT INTO t VALUES(2,'q');"
$S -init /dev/null -batch main.db "SELECT a, b FROM t ORDER BY a;"
echo "back=$($S -init /dev/null -batch main.db 'PRAGMA journal_mode = delete;')"
echo "restored_versions=$(vers)"
$S -init /dev/null -batch main.db "SELECT a, b FROM t ORDER BY a;"
