# R08.6: a DELETE removes the deleted row's index entries, and stock sqlite3
# finds no orphan entry when it runs PRAGMA integrity_check over the file.
#
# What the lines observe. Stock sqlite3 builds the table and the index, so the
# index is known-good before the engine touches it. The engine then deletes two
# rows and looks each deleted key up again: `gone-20:` and `gone-30:` are the
# index-driven lookups of the removed keys and must find nothing, `kept-40:`
# shows a surviving key is still reachable through the same index, and `rows:`
# is the table's own count. `integrity:` is the row this requirement names as
# its observable -- stock sqlite3 re-opening the file and walking every index
# against every table. `sqlite-b20:` and `sqlite-rows:` are stock's second
# opinion on the same two questions the engine answered.
#
# Before v0.0.20 the DELETE removes the table row and leaves the index entry
# behind: the lookup of the deleted key still returns it, and stock sqlite3
# reports `wrong # of entries in index i_b`. That is the difference this case
# measures.
set -u

# Indexes are opt-in from v0.0.22 to v0.1.3 and the switch changes shape
# across that span, so ask the shell what it accepts rather than assuming. An
# engine with no such option gets no option.
IDX=""
case "$("$CLI" --help 2>&1)" in
  *"--experimental-indexes <"*) IDX="--experimental-indexes=true" ;;
  *"--experimental-indexes"*)   IDX="--experimental-indexes" ;;
esac

"$SQLITE3" -batch main.db <<'SQL'
CREATE TABLE t(a INTEGER, b INTEGER);
INSERT INTO t VALUES(1,10),(2,20),(3,30),(4,40);
CREATE INDEX i_b ON t(b);
SQL

"$CLI" -m "$CLI_MODE" $IDX -q main.db 2>/dev/null <<'SQL' | sed '/^[[:space:]]*│/d; /^[[:space:]]*$/d; s/^.*×.*$/ERROR/'
DELETE FROM t WHERE b=20;
DELETE FROM t WHERE b=30;
SELECT 'gone-20:'||count(*) FROM t WHERE b=20;
SELECT 'gone-30:'||count(*) FROM t WHERE b=30;
SELECT 'kept-40:'||count(*) FROM t WHERE b=40;
SELECT 'rows:'||count(*) FROM t;
SQL

echo "sqlite-integrity:$("$SQLITE3" -batch -list -noheader main.db 'PRAGMA integrity_check;' 2>&1)"
"$SQLITE3" -batch -list -noheader main.db 2>&1 <<'SQL' | sed 's/^/sqlite:/'
SELECT 'b20:'||count(*) FROM t WHERE b=20;
SELECT 'b40:'||count(*) FROM t WHERE b=40;
SELECT 'rows:'||count(*) FROM t;
SELECT 'indexes:'||count(*) FROM sqlite_schema WHERE type='index';
SQL
