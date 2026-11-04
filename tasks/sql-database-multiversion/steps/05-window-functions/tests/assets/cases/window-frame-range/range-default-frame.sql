-- windowfunctions.html, Frame Specifications: "The default frame-spec is:
-- RANGE BETWEEN UNBOUNDED PRECEDING AND CURRENT ROW", and "The default means
-- that aggregate window functions read all rows from the beginning of the
-- partition up to and including the current row and its peers."
-- (PROFILE.md 5.3)
CREATE TABLE t1(a INTEGER PRIMARY KEY, b, c);
INSERT INTO t1 VALUES (1, 'A', 'one' ),
                      (2, 'B', 'two' ),
                      (3, 'C', 'three'),
                      (4, 'D', 'one' ),
                      (5, 'E', 'two' ),
                      (6, 'F', 'three'),
                      (7, 'G', 'one' );
-- a window-defn with no frame-spec and the explicit default agree, row by row.
-- This is the page's second worked example, ORDER BY c, observed with
-- order-independent aggregates: PROFILE.md 5.13 keeps its group_concat column
-- out of scope because ORDER BY c does not order the peers among themselves.
SELECT a, c, count(*) OVER (ORDER BY c), sum(a) OVER (ORDER BY c)
  FROM t1 ORDER BY a;
SELECT a, c, count(*) OVER (ORDER BY c RANGE BETWEEN UNBOUNDED PRECEDING AND CURRENT ROW),
             sum(a) OVER (ORDER BY c RANGE BETWEEN UNBOUNDED PRECEDING AND CURRENT ROW)
  FROM t1 ORDER BY a;
-- "This implies that rows that have the same values for all ORDER BY
-- expressions will also have the same value for the result of the window
-- function (as the window frame is the same)": three distinct values for
-- seven rows, one per peer group
SELECT DISTINCT count(*) OVER (ORDER BY c) AS n FROM t1 ORDER BY n;
