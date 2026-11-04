# R08.9: the ALTER TABLE form supported at this checkpoint -- RENAME TO -- runs,
# and the changed schema is visible in a subsequent query of sqlite_schema.
#
# What the lines observe. `schema:` is the engine reading the table name back
# out of sqlite_schema after the rename, `data:` and `count:` show the rows
# came with the name rather than being left behind under the old one, and
# `sqlite-reads:` is stock sqlite3 re-opening the file and finding the same
# table under the new name -- so the rename is in the file, not only in the
# engine's cache.
#
# The stored `sql` text of the renamed table is not asserted. spec/PROFILE.md
# rule 2 fixes how schema text is re-printed, but nothing in this step's
# requirement list fixes what a rename does to it, so grading it would pin a
# choice no document states.
#
# RENAME TO is the only ALTER form this checkpoint has: ADD COLUMN, RENAME
# COLUMN and DROP COLUMN all abort the process with `not yet implemented` at
# exit 101 rather than reporting a refusal, which is why no case here grades a
# rejected ALTER form. Step 10 is where the rest of ALTER TABLE arrives.
#
# At v0.0.19 the statement is entirely unimplemented. Stock sqlite3 creates the
# table; the engine renames it.
set -u

"$SQLITE3" -batch main.db <<'SQL'
CREATE TABLE old(a INTEGER, b TEXT);
INSERT INTO old VALUES(1,'x'),(2,'y');
SQL

"$CLI" -m "$CLI_MODE" -q main.db 2>/dev/null <<'SQL' | sed '/^[[:space:]]*│/d; /^[[:space:]]*$/d; s/^.*×.*$/ERROR/'
ALTER TABLE old RENAME TO new;
SELECT 'schema:'||name FROM sqlite_schema WHERE type='table' ORDER BY name;
SELECT 'data:'||a||b FROM new ORDER BY a;
SELECT 'count:'||count(*) FROM new;
SQL

echo "sqlite-reads:$("$SQLITE3" -batch -list -noheader main.db 'SELECT count(*) FROM new;')"
