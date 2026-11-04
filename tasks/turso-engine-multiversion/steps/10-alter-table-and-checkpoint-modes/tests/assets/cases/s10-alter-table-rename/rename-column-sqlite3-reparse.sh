# R10.2: RENAME COLUMN rewrites the index definition that names the column as
# well as the CREATE TABLE text, and stock sqlite3 re-parses both: the index
# is keyed on the new name, an equality lookup on the new name finds the row,
# and integrity_check is ok.
#
# The previous checkpoint aborts the process on RENAME COLUMN. What is graded
# here is the file the engine leaves behind, read by a reader that knows
# nothing about the ALTER: stock sqlite3 must see a table whose second column
# is `label`, an index whose keyed column is `label`, and the same three rows.
#
# Indexes are opt-in from v0.0.22 to v0.1.3 and the switch changes shape
# across that span, so ask the shell what it accepts rather than assuming. An
# engine with no such option gets no option.
set -u
IDX=""
case "$("$CLI" --help 2>&1)" in
  *"--experimental-indexes <"*) IDX="--experimental-indexes=true" ;;
  *"--experimental-indexes"*)   IDX="--experimental-indexes" ;;
esac

"$CLI" -m "$CLI_MODE" $IDX -q main.db > /dev/null 2>&1 <<'SQL'
CREATE TABLE t(a INTEGER, b TEXT);
CREATE INDEX idx_b ON t(b);
INSERT INTO t VALUES(1,'x'),(2,'y'),(3,'z');
ALTER TABLE t RENAME COLUMN b TO label;
-- The writing invocation ends by reading back every table it wrote. A
-- conforming engine does not need that: spec/PROFILE.md's durability rule
-- requires a process that writes and then exits to leave its committed data
-- recoverable by the next reader that opens the files. The read-back is this
-- case's own belt and braces, its output is discarded, and nothing below
-- depends on what it printed.
SELECT count(*) FROM t;
SQL
# Stock sqlite3's stderr is merged on purpose: a stock-side failure must land
# in the graded output rather than vanish.
echo "integrity: $("$SQLITE3" -batch main.db 'PRAGMA integrity_check;' 2>&1)"
echo "schema:"
"$SQLITE3" -batch -list -separator '|' -noheader main.db \
    'SELECT type, name, sql FROM sqlite_schema ORDER BY type, name;' 2>&1
echo "columns:"
"$SQLITE3" -batch -list -separator '|' -noheader main.db \
    "SELECT cid, name FROM pragma_table_info('t') ORDER BY cid;" 2>&1
echo "index_keyed_on:"
"$SQLITE3" -batch -list -separator '|' -noheader main.db \
    "SELECT name FROM pragma_index_info('idx_b');" 2>&1
echo "lookup_by_new_name:"
"$SQLITE3" -batch -list -separator '|' -noheader main.db \
    "SELECT a FROM t WHERE label = 'y';" 2>&1
echo "rows:"
"$SQLITE3" -batch -list -separator '|' -noheader main.db \
    'SELECT a, label FROM t ORDER BY a;' 2>&1
