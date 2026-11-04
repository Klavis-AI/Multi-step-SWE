
# R12.5: which rows an ORDER BY vector_distance_l2(...) LIMIT k query returns
# over a table of stored vectors, and in which order.
# A k-nearest-neighbour query is the point of the distance function: order a
# table of stored vectors by their distance to a probe and take the first k.
# The order is pinned by the distance and then by id, so no two rows can tie
# into an unspecified order.
#
# .sh rather than .sql: the distance is a REAL and its rendering is inside the
# graded stdout, which a .sql case would compare against stock sqlite3 -- and
# stock sqlite3 cannot evaluate the function at all.
set -u
set -o pipefail
"$CLI" -m "$CLI_MODE" -q main.db <<'SQL' | sed '/^[[:space:]]*$/d'
CREATE TABLE v(id INTEGER PRIMARY KEY, e BLOB);
INSERT INTO v VALUES
  (1, vector('[0,0]')),
  (2, vector('[1,0]')),
  (3, vector('[0,3]')),
  (4, vector('[4,4]')),
  (5, vector('[0,2]')),
  (6, vector('[10,10]'));
SELECT id, vector_distance_l2(e, vector('[0,0]')) AS d FROM v ORDER BY d, id LIMIT 3;
SELECT id FROM v ORDER BY vector_distance_l2(e, vector('[10,10]')), id LIMIT 2;
SELECT count(*) FROM v;
SQL
