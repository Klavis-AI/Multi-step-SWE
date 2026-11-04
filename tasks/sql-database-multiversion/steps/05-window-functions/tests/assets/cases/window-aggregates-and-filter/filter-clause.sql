-- windowfunctions.html, The FILTER Clause: "If a FILTER clause is provided,
-- then only rows for which the expr is true are included in the window frame.
-- The aggregate window still returns a value for every row, but those for
-- which the FILTER expression evaluates to other than true are not included in
-- the window frame for any row."  The first query is the page's own worked
-- example.  (PROFILE.md 5.5)
CREATE TABLE t1(a INTEGER PRIMARY KEY, b, c);
INSERT INTO t1 VALUES (1, 'A', 'one' ),
                      (2, 'B', 'two' ),
                      (3, 'C', 'three'),
                      (4, 'D', 'one' ),
                      (5, 'E', 'two' ),
                      (6, 'F', 'three'),
                      (7, 'G', 'one' );
SELECT c, a, b, group_concat(b, '.') FILTER (WHERE c!='two') OVER (
  ORDER BY a
) AS group_concat
FROM t1 ORDER BY a;
-- "still returns a value for every row": seven rows in, seven rows out
SELECT count(*) FROM (SELECT count(*) FILTER (WHERE c!='two') OVER (ORDER BY a) FROM t1);
-- a FILTER that admits nothing leaves an empty frame for every row
SELECT a, count(*) FILTER (WHERE a > 100) OVER (ORDER BY a) FROM t1 ORDER BY a;
-- and one that admits everything is the same as no FILTER at all
SELECT a, count(*) FILTER (WHERE a > 0) OVER (ORDER BY a),
          count(*) OVER (ORDER BY a)
  FROM t1 ORDER BY a;
-- FILTER applies to the frame, so it composes with an explicit frame-spec
SELECT a, sum(a) FILTER (WHERE c!='two') OVER (ORDER BY a ROWS BETWEEN 1 PRECEDING AND 1 FOLLOWING)
  FROM t1 ORDER BY a;
