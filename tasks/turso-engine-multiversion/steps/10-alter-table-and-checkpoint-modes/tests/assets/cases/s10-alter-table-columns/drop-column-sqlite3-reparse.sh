# R10.5: after DROP COLUMN the file satisfies a reader that knows nothing
# about the ALTER -- stock sqlite3 re-parses the narrowed schema, reports the
# reduced column list, reads the narrowed records back, and integrity_check
# is ok.
#
# After DROP COLUMN the file must satisfy a reader that knows nothing about
# the ALTER: stock sqlite3 re-parses the narrowed schema, reports the reduced
# column list, reads the narrowed records back and passes integrity_check.
#
# The stored `sql` text is deliberately not read back here. At this checkpoint
# the text DROP COLUMN rebuilds carries a space inside each parenthesis,
# contrary to spec/PROFILE.md rule 2, and that is corrected later in the chain
# (step 12, R12.6); the column list and the records are what this step grades.
set -u
"$CLI" -m "$CLI_MODE" -q main.db > /dev/null 2>&1 <<'SQL'
CREATE TABLE t(a INTEGER, b TEXT, c INTEGER);
INSERT INTO t VALUES(1,'x',10),(2,'y',20),(3,'z',30);
ALTER TABLE t DROP COLUMN b;
-- The writing invocation ends by reading back every table it wrote. A
-- conforming engine does not need that: spec/PROFILE.md's durability rule
-- requires a process that writes and then exits to leave its committed data
-- recoverable by the next reader that opens the files. The read-back is this
-- case's own belt and braces, its output is discarded, and nothing below
-- depends on what it printed.
SELECT count(*) FROM t;
SQL
echo "integrity: $("$SQLITE3" -batch main.db 'PRAGMA integrity_check;' 2>&1)"
echo "columns:"
"$SQLITE3" -batch -list -separator '|' -noheader main.db \
    "SELECT cid, name, type FROM pragma_table_info('t') ORDER BY cid;" 2>&1
echo "rows:"
"$SQLITE3" -batch -list -separator '|' -noheader main.db \
    'SELECT a, c FROM t ORDER BY a;' 2>&1
echo "widths:"
"$SQLITE3" -batch -list -separator '|' -noheader main.db \
    "SELECT count(*) FROM pragma_table_info('t');" 2>&1
