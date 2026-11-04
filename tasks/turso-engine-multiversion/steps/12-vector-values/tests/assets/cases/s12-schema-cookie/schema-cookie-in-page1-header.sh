
# R12.2 and R12.1: bytes 40..43 of the page-1 header, read with od after each of
# two schema changes, and stock sqlite3's PRAGMA schema_version for the same
# file. What is observed is the field in the file, not only what the engine
# says about it, and that the two agree.
# Bytes 40..43 of the page-1 header are the schema cookie. Every reader that
# caches a parsed schema relies on it changing when the schema does; an engine
# that leaves it at zero will hand a stale schema to anyone who trusts it.
# This checkpoint starts incrementing it, once per schema change.
set -u
"$CLI" -m "$CLI_MODE" -q main.db > /dev/null 2>&1 <<'SQL'
CREATE TABLE a(x);
INSERT INTO a VALUES(1);
-- The writing invocation ends by reading back every table it wrote. A
-- conforming engine does not need that: spec/PROFILE.md's durability rule
-- requires a process that writes and then exits to leave its committed data
-- recoverable by the next reader that opens the files. The read-back is this
-- case's own belt and braces, its output is discarded, and nothing below
-- depends on what it printed.
SELECT count(*) FROM a;
SQL
echo "after_one_table $(od -An -tx1 -j 40 -N 4 main.db | tr -s ' \n' ' ' | sed 's/^ //;s/ $//')"
"$CLI" -m "$CLI_MODE" -q main.db > /dev/null 2>&1 <<'SQL'
CREATE TABLE b(y);
INSERT INTO b VALUES(2);
SELECT count(*) FROM a;
SELECT count(*) FROM b;
SQL
echo "after_two_tables $(od -An -tx1 -j 40 -N 4 main.db | tr -s ' \n' ' ' | sed 's/^ //;s/ $//')"
echo "sqlite_schema_version $("$SQLITE3" -batch -list -noheader main.db 'PRAGMA schema_version;' 2>&1)"
echo "integrity $("$SQLITE3" -batch main.db 'PRAGMA integrity_check;' 2>&1)"
