-- PROFILE.md 5.1: every part of a window-defn is optional, so OVER () is a
-- legal window definition and means one partition holding every row with no
-- ordering.  windowfunctions.html: "if there are no ORDER BY terms, all rows
-- are peers", and of rank() and dense_rank(), "If there is no ORDER BY clause,
-- then all rows are considered peers and this function always returns 1".
CREATE TABLE t(k INTEGER PRIMARY KEY, v INTEGER);
INSERT INTO t VALUES(1,10),(2,20),(3,30),(4,40);
SELECT k, count(*) OVER (), sum(v) OVER (), min(v) OVER (), max(v) OVER ()
  FROM t ORDER BY k;
SELECT k, rank() OVER (), dense_rank() OVER () FROM t ORDER BY k;
-- an ordinary aggregate and the same aggregate as a window function over the
-- whole table agree
SELECT sum(v) FROM t;
SELECT DISTINCT sum(v) OVER () FROM t;
