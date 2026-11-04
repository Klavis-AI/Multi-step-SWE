# R09.10: PRAGMA user_version written by the engine and read back from the file by stock sqlite3.
# PRAGMA user_version round-trips a value through the page-1 header, and the
# value is written into the file rather than held in memory.
#
# At v0.0.20 the assignment form panics. The file is created by stock sqlite3
# and the read-back after the engine's write is done by stock sqlite3.
set -u

"$SQLITE3" -batch main.db <<'SQL'
CREATE TABLE t(a INTEGER);
INSERT INTO t VALUES(1),(2);
SQL

"$CLI" -m "$CLI_MODE" -q main.db 2>/dev/null <<'SQL' | sed '/^[[:space:]]*│/d; /^[[:space:]]*$/d; s/^.*×.*$/ERROR/'
PRAGMA user_version;
PRAGMA user_version = 42;
PRAGMA user_version;
SELECT 'table:'||count(*) FROM t;
SQL

echo "sqlite-user-version:$("$SQLITE3" -batch -list -noheader main.db 'PRAGMA user_version;')"
echo "sqlite-table-count:$("$SQLITE3" -batch -list -noheader main.db 'SELECT count(*) FROM t;')"
