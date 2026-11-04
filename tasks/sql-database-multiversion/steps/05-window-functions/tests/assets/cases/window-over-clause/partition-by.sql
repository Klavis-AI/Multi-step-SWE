-- windowfunctions.html, The PARTITION BY Clause: "the rows returned by the
-- SELECT statement are divided into groups - partitions - with the same values
-- for each PARTITION BY expression, and then window-function processing is
-- performed separately for each partition."  This is the page's own worked
-- example, with the total ORDER BY it already carries.  (PROFILE.md 5.2)
CREATE TABLE t1(a INTEGER PRIMARY KEY, b, c);
INSERT INTO t1 VALUES (1, 'A', 'one' ),
                      (2, 'B', 'two' ),
                      (3, 'C', 'three'),
                      (4, 'D', 'one' ),
                      (5, 'E', 'two' ),
                      (6, 'F', 'three'),
                      (7, 'G', 'one' );
SELECT c, a, b, group_concat(b, '.') OVER (
  PARTITION BY c ORDER BY a RANGE BETWEEN CURRENT ROW AND UNBOUNDED FOLLOWING
) AS group_concat
FROM t1 ORDER BY c, a;
-- the same query without PARTITION BY is the page's other example: one
-- partition holding all seven rows
SELECT c, a, b, group_concat(b, '.') OVER (
  ORDER BY c, a ROWS BETWEEN CURRENT ROW AND UNBOUNDED FOLLOWING
) AS group_concat
FROM t1 ORDER BY c, a;
