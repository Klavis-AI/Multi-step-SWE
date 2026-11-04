# R08.12: PRAGMA schema_version returns the schema cookie stored in the page-1
# header, and returns the same value stock sqlite3 reads from the same file.
#
# What the lines observe. The first line is the engine's answer to the pragma
# over a file stock sqlite3 created; `table-row:` shows the same invocation
# also queried a table in it, so the cookie was read from an opened database;
# `sqlite-schema-version:` is stock's reading of the same header field. The two
# numbers agreeing is the assertion.
#
# Stock makes five schema changes while building the file, so the cookie it
# leaves at page-1 offset 40 is 5. That is deliberate. An earlier version of
# this case created a single table, leaving the cookie at 1 -- and 1 is the
# value an engine that never read the header at all would be most likely to
# print, so the comparison could be satisfied by a constant. At 5 the line can
# only come from the header of the file that was opened, which is what R08.12
# says. The five changes are three CREATE TABLE, one CREATE INDEX and one DROP
# TABLE; the dropped table is why `table-row:` counts rows of `t` and not of
# the last table created.
#
# The case does not assert how the cookie changes as the schema is mutated --
# that is step 12's subject and its own requirement row. Here it is read, not
# advanced.
#
# This lands at v0.0.20: the previous checkpoint does not answer the pragma.
set -u

"$SQLITE3" -batch main.db <<'SQL'
CREATE TABLE t(a INTEGER);
CREATE TABLE u(b INTEGER);
CREATE TABLE v(c INTEGER);
CREATE INDEX i_a ON t(a);
DROP TABLE v;
SQL

"$CLI" -m "$CLI_MODE" -q main.db 2>/dev/null <<'SQL' | sed '/^[[:space:]]*│/d; /^[[:space:]]*$/d; s/^.*×.*$/ERROR/'
PRAGMA schema_version;
SELECT 'table-row:'||count(*) FROM t;
SQL

echo "sqlite-schema-version:$("$SQLITE3" -batch -list -noheader main.db 'PRAGMA schema_version;')"
