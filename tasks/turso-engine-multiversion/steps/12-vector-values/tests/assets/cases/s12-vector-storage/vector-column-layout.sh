
# R12.3 and R12.11: the bytes a vector column holds at each element width,
# including the empty vector at both widths, and the text vector_extract renders
# a stored vector back as.
# A vector is stored as an opaque blob, so the record holds a blob serial type
# and nothing about it is special to the record format. The layout is pinned
# by its bytes: a float32 vector is four little-endian bytes per element;
# a float64 vector is eight little-endian bytes per element followed by a
# one-byte type marker. Round-tripping through a table must not change either,
# and the distance function has to read a stored vector as readily as an
# inline one. The empty vector is the boundary of that arithmetic: at 32-bit
# width it is zero bytes, and at 64-bit width it is the marker byte alone.
#
# .sh rather than .sql because stock sqlite3 cannot evaluate vector() at all;
# the column this compares is the engine's own rendering of its own type.
set -u
set -o pipefail
"$CLI" -m "$CLI_MODE" -q main.db <<'SQL' | sed '/^[[:space:]]*$/d'
CREATE TABLE v(id INTEGER PRIMARY KEY, e BLOB);
INSERT INTO v VALUES(1, vector32('[1,2]')), (2, vector64('[1,2]')), (3, vector('[3,4]'));
SELECT id, typeof(e), length(e), hex(e) FROM v ORDER BY id;
SELECT length(vector('[]')), length(vector64('[]'));
SELECT id, vector_extract(e) FROM v ORDER BY id;
SELECT vector_distance_l2(e, vector('[0,0]')) FROM v WHERE id = 3;
SELECT count(*) FROM v;
SQL
