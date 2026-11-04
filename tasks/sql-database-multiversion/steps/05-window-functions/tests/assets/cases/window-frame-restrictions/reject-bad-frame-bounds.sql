-- windowfunctions.html, Frame Specifications: "The ending frame boundary must
-- not take a form that appears higher in the above list than the starting
-- frame boundary", and of the offset, "<expr> is a constant expression that
-- evaluates to a non-negative integer value".  The rejected statements leave
-- the database exactly as it was (PROFILE.md 5.11, TEST_INTERFACE.md 2.3).
CREATE TABLE t(k INTEGER PRIMARY KEY, v INTEGER);
INSERT INTO t VALUES(1,10),(2,20),(3,30);
-- the orderings the page allows
SELECT k, sum(v) OVER (ORDER BY k ROWS BETWEEN UNBOUNDED PRECEDING AND UNBOUNDED FOLLOWING)
  FROM t ORDER BY k;
SELECT k, sum(v) OVER (ORDER BY k ROWS BETWEEN 1 PRECEDING AND 1 FOLLOWING)
  FROM t ORDER BY k;
SELECT k, sum(v) OVER (ORDER BY k ROWS BETWEEN CURRENT ROW AND UNBOUNDED FOLLOWING)
  FROM t ORDER BY k;
-- an ending boundary higher in the list than the starting one.  Two bounds of
-- the SAME form are not "higher", and the two spellings that leaves open --
-- both bounds UNBOUNDED PRECEDING, both bounds UNBOUNDED FOLLOWING -- are
-- unscored (PROFILE.md 5.13), so no statement here writes either.
SELECT k, sum(v) OVER (ORDER BY k ROWS BETWEEN CURRENT ROW AND 1 PRECEDING)
  FROM t ORDER BY k;
SELECT k, sum(v) OVER (ORDER BY k ROWS BETWEEN 1 FOLLOWING AND CURRENT ROW)
  FROM t ORDER BY k;
-- an offset that is not a non-negative integer
SELECT k, sum(v) OVER (ORDER BY k ROWS BETWEEN -1 PRECEDING AND CURRENT ROW)
  FROM t ORDER BY k;
SELECT k, sum(v) OVER (ORDER BY k ROWS BETWEEN CURRENT ROW AND -2 FOLLOWING)
  FROM t ORDER BY k;
-- nothing above changed the table
SELECT k, v FROM t ORDER BY k;
PRAGMA integrity_check;
