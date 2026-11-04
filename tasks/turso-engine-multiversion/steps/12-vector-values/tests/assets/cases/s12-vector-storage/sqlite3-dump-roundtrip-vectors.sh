
# R12.8: stock sqlite3's .dump of a table holding vector values, and the bytes a
# fresh database re-imported from that dump holds. What is observed is the dump
# text and the hex of every re-imported value.
# .dump is the whole-file gate: stock sqlite3 has to render every vector as a
# blob literal, and re-importing that dump into a fresh database has to
# reproduce the identical bytes. If the vector layout had leaked into the
# record encoding, one of the two halves would disagree.
set -u
"$CLI" -m "$CLI_MODE" -q main.db > /dev/null 2>&1 <<'SQL'
CREATE TABLE v(id INTEGER PRIMARY KEY, e BLOB);
INSERT INTO v VALUES(1, vector('[1,2]')),(2, vector64('[1,2]'));
CREATE TABLE d(id INTEGER PRIMARY KEY, dist REAL);
INSERT INTO d SELECT id, vector_distance_l2(e, e) FROM v;
-- The writing invocation ends by reading back every table it wrote. A
-- conforming engine does not need that: spec/PROFILE.md's durability rule
-- requires a process that writes and then exits to leave its committed data
-- recoverable by the next reader that opens the files. The read-back is this
-- case's own belt and braces, its output is discarded, and nothing below
-- depends on what it printed.
SELECT count(*) FROM v;
SELECT count(*) FROM d;
SQL
"$SQLITE3" -batch main.db '.dump' > dump.sql 2>&1
cat dump.sql
"$SQLITE3" -batch copy.db < dump.sql > /dev/null 2>&1
echo "reimported:"
"$SQLITE3" -batch -list -separator '|' -noheader copy.db \
    'SELECT id, hex(e) FROM v ORDER BY id;' 2>&1
echo "identical: $("$SQLITE3" -batch -list -noheader copy.db \
    "SELECT count(*) FROM v WHERE hex(e) IN ('0000803F00000040','000000000000F03F000000000000004002');" 2>&1)"
