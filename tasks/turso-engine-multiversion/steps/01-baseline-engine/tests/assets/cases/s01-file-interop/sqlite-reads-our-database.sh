# R01.1: the second half of the schema-text requirement -- stock sqlite3
# opening the engine's file and re-parsing it to the same schema.
#
# R01.1 is graded in two halves. `s01-ddl-and-schema/schema-sql-text.sql` reads
# the `sqlite_schema.sql` rendering back through the engine's own shell, which
# is the only place that rendering is visible. This case is the other half: the
# same file is handed to stock sqlite3, whose `PRAGMA integrity_check` must
# return `ok` and whose own reader must resolve the engine-written schema well
# enough to answer `SELECT id, s FROM t ORDER BY id` -- which it can only do by
# re-parsing the stored statement text. Nothing about the engine's internals is
# asserted; only what a foreign reader sees.
# A database this engine writes must be readable by the stock sqlite3 binary:
# it must pass integrity_check and return the same rows. Nothing about the
# engine's internals is asserted -- only what a foreign reader sees.
set -u
"$CLI" -m "$CLI_MODE" -q main.db > /dev/null 2>&1 <<'SQL'
CREATE TABLE t(id INTEGER PRIMARY KEY, s TEXT);
INSERT INTO t VALUES(1,'hello'),(2,'world'),(3,'third');
-- Durability barrier. The writing invocation ends by reading back every table
-- it wrote; reading the data pages is what forces a pending write to complete,
-- and reading only sqlite_schema touches page 1 and is not sufficient.
-- It is not needed at this step's own checkpoint -- v0.0.13 finalises its WAL
-- at process exit, measured clean without the barrier -- and it is here
-- because the generator re-measures every case at every later checkpoint in
-- the chain and rejects one whose answer moves there, and v0.0.16 through
-- v0.0.18 do not finalise it at exit: a cross-process
-- write-then-read corrupts 5-25% of the time there. It asserts nothing and
-- weakens nothing; the same rows are read back either way.
SELECT count(*) FROM t;
SQL
echo "integrity: $("$SQLITE3" -batch main.db 'PRAGMA integrity_check;' 2>&1)"
echo "rows:"
"$SQLITE3" -batch -list -separator '|' -noheader main.db \
    'SELECT id,s FROM t ORDER BY id;' 2>&1
