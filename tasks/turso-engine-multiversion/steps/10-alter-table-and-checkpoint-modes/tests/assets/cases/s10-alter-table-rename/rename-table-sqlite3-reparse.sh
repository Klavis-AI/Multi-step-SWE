# R10.1: the schema text RENAME TO rewrote is re-parsable by a foreign
# reader -- stock sqlite3 opens the file, re-parses the rewritten CREATE TABLE
# text under the new name, and returns the same shape and the same rows.
#
# The renamed schema has to be re-parsable by a foreign reader, not just by
# the engine that wrote it. Stock sqlite3 opens the file, re-parses the
# rewritten CREATE TABLE text, and is asked for the schema, the table shape
# and the rows.
set -u
"$CLI" -m "$CLI_MODE" -q main.db > /dev/null 2>&1 <<'SQL'
CREATE TABLE emp(id INTEGER PRIMARY KEY, name TEXT);
INSERT INTO emp VALUES(1,'ada'),(2,'bob'),(3,'cyd');
ALTER TABLE emp RENAME TO staff;
-- The writing invocation ends by reading back every table it wrote. A
-- conforming engine does not need that: spec/PROFILE.md's durability rule
-- requires a process that writes and then exits to leave its committed data
-- recoverable by the next reader that opens the files. The read-back is this
-- case's own belt and braces, its output is discarded, and nothing below
-- depends on what it printed.
SELECT count(*) FROM staff;
SQL
echo "integrity: $("$SQLITE3" -batch main.db 'PRAGMA integrity_check;' 2>&1)"
echo "schema:"
"$SQLITE3" -batch -list -separator '|' -noheader main.db \
    'SELECT name, sql FROM sqlite_schema ORDER BY name;' 2>&1
echo "columns:"
"$SQLITE3" -batch -list -separator '|' -noheader main.db \
    "SELECT cid, name FROM pragma_table_info('staff') ORDER BY cid;" 2>&1
echo "rows:"
"$SQLITE3" -batch -list -separator '|' -noheader main.db \
    'SELECT id, name FROM staff ORDER BY id;' 2>&1
