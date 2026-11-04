-- windowfunctions.html, Frame Specifications: the five frame boundaries, in a
-- ROWS frame, written in every position the page allows.  The window ORDER BY
-- is on the PRIMARY KEY so it totally orders the rows and there are no peers
-- to disambiguate (PROFILE.md 5.3, 5.8).
CREATE TABLE t(k INTEGER PRIMARY KEY, v INTEGER);
INSERT INTO t VALUES(1,1),(2,2),(3,4),(4,8),(5,16),(6,32);
-- "UNBOUNDED PRECEDING: The start of the frame is the first row in the set."
SELECT k, sum(v) OVER (ORDER BY k ROWS BETWEEN UNBOUNDED PRECEDING AND CURRENT ROW)
  FROM t ORDER BY k;
-- "<expr> PRECEDING ... <expr> rows before the current row" and
-- "<expr> FOLLOWING ... <expr> rows after the current row"
SELECT k, sum(v) OVER (ORDER BY k ROWS BETWEEN 1 PRECEDING AND 1 FOLLOWING)
  FROM t ORDER BY k;
-- "UNBOUNDED FOLLOWING: The end of the frame is the last row in the set."
SELECT k, sum(v) OVER (ORDER BY k ROWS BETWEEN CURRENT ROW AND UNBOUNDED FOLLOWING)
  FROM t ORDER BY k;
SELECT k, sum(v) OVER (ORDER BY k ROWS BETWEEN UNBOUNDED PRECEDING AND UNBOUNDED FOLLOWING)
  FROM t ORDER BY k;
-- both boundaries before the current row, and both after it; count(*) rather
-- than sum() because these frames are empty at one end of the partition
SELECT k, count(*) OVER (ORDER BY k ROWS BETWEEN 2 PRECEDING AND 1 PRECEDING)
  FROM t ORDER BY k;
SELECT k, count(*) OVER (ORDER BY k ROWS BETWEEN 1 FOLLOWING AND 2 FOLLOWING)
  FROM t ORDER BY k;
