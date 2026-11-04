-- PROFILE.md 5.2: a window-defn may carry PARTITION BY with no ORDER BY, in
-- which case every row of a partition is a peer of every other and the frame
-- is the whole partition.  A PARTITION BY term is an expression, like a
-- GROUP BY term.
CREATE TABLE t(k INTEGER PRIMARY KEY, g TEXT, v INTEGER);
INSERT INTO t VALUES(1,'x',10),(2,'x',20),(3,'y',30),(4,'y',40),(5,'y',50);
SELECT k, g, count(*) OVER (PARTITION BY g), sum(v) OVER (PARTITION BY g)
  FROM t ORDER BY k;
-- partitioning by an expression rather than a column
SELECT k, v, sum(v) OVER (PARTITION BY v > 25) FROM t ORDER BY k;
-- two partitioning terms
SELECT k, g, v, count(*) OVER (PARTITION BY g, v > 35) FROM t ORDER BY k;
-- "each partition is processed separately": a partitioned aggregate window
-- function agrees with the same aggregate under GROUP BY
SELECT g, sum(v) FROM t GROUP BY g ORDER BY g;
