-- windowfunctions.html, Frame Specifications: "The ending frame boundary can
-- be omitted, in which case it defaults to CURRENT ROW", and both
-- "0 PRECEDING" and "0 FOLLOWING" are "the same as CURRENT ROW".  Each pair
-- below is the short form beside the long form it stands for.  (PROFILE.md 5.3)
CREATE TABLE t(k INTEGER PRIMARY KEY, v INTEGER);
INSERT INTO t VALUES(1,1),(2,2),(3,4),(4,8),(5,16);
SELECT k, sum(v) OVER (ORDER BY k ROWS UNBOUNDED PRECEDING),
          sum(v) OVER (ORDER BY k ROWS BETWEEN UNBOUNDED PRECEDING AND CURRENT ROW)
  FROM t ORDER BY k;
SELECT k, sum(v) OVER (ORDER BY k ROWS 2 PRECEDING),
          sum(v) OVER (ORDER BY k ROWS BETWEEN 2 PRECEDING AND CURRENT ROW)
  FROM t ORDER BY k;
SELECT k, v, sum(v) OVER (ORDER BY k ROWS CURRENT ROW),
             sum(v) OVER (ORDER BY k ROWS BETWEEN 0 PRECEDING AND 0 FOLLOWING),
             sum(v) OVER (ORDER BY k ROWS BETWEEN CURRENT ROW AND CURRENT ROW)
  FROM t ORDER BY k;
