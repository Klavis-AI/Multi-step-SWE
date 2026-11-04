
# R12.9 and R12.7: what a reader that has never heard of the vector type sees --
# stock sqlite3's typeof, length and hex for a vector column, and its
# integrity_check verdict on the file that holds them.
# A vector is a type this engine invented; a reader that has never heard of it
# must still be able to open the file. Stock sqlite3 sees the column as an
# ordinary blob, reports the same bytes and the same length, and returns ok
# from integrity_check. The distances computed by the engine are materialised
# into a second table so that what sqlite3 reads back includes the result of
# the new function, not only its inputs.
#
# One of the three distances is sqrt(5), which no binary float represents
# exactly. It is read back through round(dist, 6), the comparison the "Vector
# values" section of TEST_INTERFACE.md states for an inexact distance: six
# decimal places is coarser than the spacing of a 32-bit float there, so the
# value asserted is the same whichever width the engine accumulates in. What
# is graded is still the storage class -- typeof is real, not text or blob.
set -u
"$CLI" -m "$CLI_MODE" -q main.db > /dev/null 2>&1 <<'SQL'
CREATE TABLE v(id INTEGER PRIMARY KEY, e BLOB);
INSERT INTO v VALUES(1, vector('[0,0]')),(2, vector('[3,4]')),(3, vector('[1,2]'));
CREATE TABLE d(id INTEGER PRIMARY KEY, dist REAL);
INSERT INTO d SELECT id, vector_distance_l2(e, vector('[0,0]')) FROM v;
-- The writing invocation ends by reading back every table it wrote. A
-- conforming engine does not need that: spec/PROFILE.md's durability rule
-- requires a process that writes and then exits to leave its committed data
-- recoverable by the next reader that opens the files. The read-back is this
-- case's own belt and braces, its output is discarded, and nothing below
-- depends on what it printed.
SELECT count(*) FROM v;
SELECT count(*) FROM d;
SQL
echo "integrity: $("$SQLITE3" -batch main.db 'PRAGMA integrity_check;' 2>&1)"
echo "vectors:"
"$SQLITE3" -batch -list -separator '|' -noheader main.db \
    'SELECT id, typeof(e), length(e), hex(e) FROM v ORDER BY id;' 2>&1
echo "distances:"
"$SQLITE3" -batch -list -separator '|' -noheader main.db \
    'SELECT id, typeof(dist), round(dist, 6) FROM d ORDER BY id;' 2>&1
