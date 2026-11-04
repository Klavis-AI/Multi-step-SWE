# R13.7, R13.9: stock sqlite3 reads a capture-enabled database as an ordinary
# one, and the time column is an integer rather than text.
# The capture table has no private storage of its own, so the stock 3.50.2
# binary opens a file this engine wrote, answers `ok` to PRAGMA
# integrity_check, finds both tables in the schema, and selects every capture
# row. It reports typeof(change_time) as `integer`: integrity_check is what
# would fail if the column held text against its declared INTEGER type. The
# value itself is a wall clock and is never selected.
set -u
"$CLI" -m "$CLI_MODE" -q main.db > /dev/null 2>&1 <<'SQL'
CREATE TABLE t(id INTEGER PRIMARY KEY, v TEXT);
PRAGMA unstable_capture_data_changes_conn('full');
INSERT INTO t VALUES(1,'one');
INSERT INTO t VALUES(2,'two');
UPDATE t SET v='TWO' WHERE id=2;
DELETE FROM t WHERE id=1;
-- The writing invocation reads back every table it wrote before it exits, as
-- spec/PROFILE.md's durability rule requires of a writer. Reading the data
-- pages is what settles the write; reading only sqlite_schema touches page 1
-- and is not enough. The subject of this case is what stock sqlite3 then
-- sees, not the commit path an earlier step already grades.
SELECT count(*) FROM t;
SELECT count(*) FROM turso_cdc;
SQL
echo "integrity: $("$SQLITE3" -batch main.db 'PRAGMA integrity_check;' 2>&1)"
echo "schema:"
"$SQLITE3" -batch -list -separator '|' -noheader main.db \
    "SELECT type,name FROM sqlite_schema WHERE name IN ('t','turso_cdc') ORDER BY name;" 2>&1
echo "capture:"
"$SQLITE3" -batch -list -separator '|' -noheader main.db \
    "SELECT change_id,change_type,table_name,id,typeof(change_time) FROM turso_cdc ORDER BY change_id;" 2>&1
echo "table:"
"$SQLITE3" -batch -list -separator '|' -noheader main.db \
    'SELECT id,v FROM t ORDER BY id;' 2>&1
