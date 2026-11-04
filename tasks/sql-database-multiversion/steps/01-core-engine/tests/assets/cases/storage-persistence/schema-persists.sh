set -u
S="$SQLITE3"
$S -init /dev/null -batch main.db "CREATE TABLE t(a INTEGER PRIMARY KEY, b TEXT); CREATE INDEX i ON t(b); PRAGMA user_version = 7;" || exit 1
$S -init /dev/null -batch main.db "SELECT type, name FROM sqlite_master ORDER BY name;"
$S -init /dev/null -batch main.db "PRAGMA user_version;"
$S -init /dev/null -batch main.db "PRAGMA table_info(t);"
