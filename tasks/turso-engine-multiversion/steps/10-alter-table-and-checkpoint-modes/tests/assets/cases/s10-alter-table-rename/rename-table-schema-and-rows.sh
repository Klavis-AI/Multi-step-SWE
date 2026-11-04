# R10.1: RENAME TO rewrites the stored schema text rather than relabelling
# the table in memory -- sqlite_schema's type, name, tbl_name and sql all
# carry the new name, and the rows survive the rename.
#
# ALTER TABLE ... RENAME TO must rewrite the stored schema text, not just
# relabel the table in memory: the new name has to appear in sqlite_schema in
# a form anything re-parsing the file accepts, and the rows have to survive.
#
# The `sql` column is graded against spec/PROFILE.md rule 2's rendering, which
# is the engine's own normalisation and not stock sqlite3's verbatim text --
# stock quotes the renamed identifier (`CREATE TABLE "staff"(...)`). The
# re-parsability that matters is asserted by the sibling case
# rename-table-sqlite3-reparse.sh, which hands the file to stock sqlite3.
#
# This is a .sh case rather than a .sql case because at this checkpoint the
# shell prints a blank separator line before every result block, which a .sql
# case is not allowed to carry. The case strips those separators itself, so
# the observation is the sequence of result lines and nothing else.
set -u
set -o pipefail
"$CLI" -m "$CLI_MODE" -q main.db <<'SQL' | sed '/^[[:space:]]*$/d'
CREATE TABLE emp(id INTEGER PRIMARY KEY, name TEXT);
INSERT INTO emp VALUES(1,'ada'),(2,'bob');
ALTER TABLE emp RENAME TO staff;
SELECT type, name, tbl_name, sql FROM sqlite_schema;
SELECT id, name FROM staff ORDER BY id;
SELECT count(*) FROM staff;
SQL
# Stock sqlite3 runs the same statements on its own file. A .sh case is outside
# the generator's automatic comparison, so the case makes it itself. The `sql`
# column is the one thing left out of that comparison, and it is left out for a
# stated reason: stock quotes the renamed identifier (`CREATE TABLE "staff"
# (...)`) where spec/PROFILE.md rule 2 fixes this engine's rendering, so the two
# cannot agree on that text by construction. Everything else the engine is
# graded on above -- that sqlite_schema's `type`, `name` and `tbl_name` all
# carry the new name, that the rows survive the rename, and the count -- stock
# must produce identically. That the rewritten `sql` text is itself re-parsable
# is the sibling case rename-table-sqlite3-reparse.sh, which hands stock the
# engine's own file. Stock's stderr is merged on purpose: a stock-side failure
# must land in the graded output rather than vanish.
echo "stock_sqlite3:"
"$SQLITE3" -batch -list -separator '|' -noheader stock.db 2>&1 <<'SQL'
CREATE TABLE emp(id INTEGER PRIMARY KEY, name TEXT);
INSERT INTO emp VALUES(1,'ada'),(2,'bob');
ALTER TABLE emp RENAME TO staff;
SELECT type, name, tbl_name FROM sqlite_schema;
SELECT id, name FROM staff ORDER BY id;
SELECT count(*) FROM staff;
SQL
