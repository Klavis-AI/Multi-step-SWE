# R14.1, R14.8: stock sqlite3 re-parses the view text this engine stored, returns the view's rows, and agrees the view's root page is zero.
# The text this engine stores for a view must be text stock sqlite3 re-parses
# as the same view: sqlite3 opening the file resolves the view, returns the
# rows the view defines, agrees that its root page is zero, and still reports
# an intact database.
set -u
"$CLI" -m "$CLI_MODE" -q main.db > /dev/null 2>&1 <<'SQL'
CREATE TABLE emp(id INTEGER PRIMARY KEY, dept TEXT, sal INTEGER);
INSERT INTO emp VALUES(1,'eng',100),(2,'eng',250),(3,'ops',50);
CREATE VIEW well_paid AS SELECT id, dept FROM emp WHERE sal >= 100;
-- Durability handoff. `spec/PROFILE.md`'s durability rule puts the obligation
-- on the writing invocation: it must not return until its committed data is
-- recoverable from the files on disk, so the stock reads below see a complete
-- file. This closing SELECT is the writing batch's own read-back of the table
-- it just wrote, and it is kept deliberately: a shell that meets the
-- durability rule loses nothing by it, and it makes the batch touch the data
-- pages and not only the schema page before it exits. Its output is discarded
-- -- only the stock reader's output below is compared.
SELECT count(*) FROM emp;
SQL
echo "integrity: $("$SQLITE3" -batch main.db 'PRAGMA integrity_check;' 2>&1)"
echo "schema:"
"$SQLITE3" -batch -list -separator '|' -noheader main.db \
    'SELECT type,name,tbl_name,rootpage FROM sqlite_schema ORDER BY name;' 2>&1
echo "view rows:"
"$SQLITE3" -batch -list -separator '|' -noheader main.db \
    'SELECT id,dept FROM well_paid ORDER BY id;' 2>&1
