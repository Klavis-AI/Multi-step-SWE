-- windowfunctions.html, Aggregate Window Functions: the page's first worked
-- frame example, run as the page writes it except for the total ORDER BY that
-- TEST_INTERFACE.md section 5 requires on the outer SELECT.  The page states
-- the result and explains it: "the frame for the row with (a=3) consists of
-- rows (2, 'B', 'two'), (3, 'C', 'three') and (4, 'D', 'one').  The result of
-- group_concat(b, '.') for that row is therefore 'B.C.D'."  (PROFILE.md 5.3)
CREATE TABLE t1(a INTEGER PRIMARY KEY, b, c);
INSERT INTO t1 VALUES (1, 'A', 'one' ),
                      (2, 'B', 'two' ),
                      (3, 'C', 'three'),
                      (4, 'D', 'one' ),
                      (5, 'E', 'two' ),
                      (6, 'F', 'three'),
                      (7, 'G', 'one' );
SELECT a, b, group_concat(b, '.') OVER (
  ORDER BY a ROWS BETWEEN 1 PRECEDING AND 1 FOLLOWING
) AS group_concat FROM t1 ORDER BY a;
-- "adding it to a query does not change the number of rows returned"
SELECT count(*) FROM t1;
SELECT count(*) FROM (SELECT a, sum(a) OVER (ORDER BY a ROWS 1 PRECEDING) FROM t1);
