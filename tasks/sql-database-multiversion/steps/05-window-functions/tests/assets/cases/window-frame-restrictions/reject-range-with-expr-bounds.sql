-- windowfunctions.html, Frame Specifications: of "<expr> PRECEDING" and
-- "<expr> FOLLOWING", "This frame boundary type may only be used with ROWS
-- frames."  The rejected statements leave the database exactly as it was
-- (PROFILE.md 5.11, TEST_INTERFACE.md 2.3).
CREATE TABLE t(k INTEGER PRIMARY KEY, v INTEGER);
INSERT INTO t VALUES(1,10),(2,20),(3,30);
-- the same two boundaries are legal in a ROWS frame
SELECT k, sum(v) OVER (ORDER BY k ROWS BETWEEN 1 PRECEDING AND 1 FOLLOWING)
  FROM t ORDER BY k;
-- and a RANGE frame is legal with the boundaries that are not offsets
SELECT k, sum(v) OVER (ORDER BY k RANGE BETWEEN UNBOUNDED PRECEDING AND CURRENT ROW)
  FROM t ORDER BY k;
-- <expr> PRECEDING in a RANGE frame is rejected
SELECT k, sum(v) OVER (ORDER BY k RANGE BETWEEN 1 PRECEDING AND CURRENT ROW)
  FROM t ORDER BY k;
-- <expr> FOLLOWING in a RANGE frame is rejected
SELECT k, sum(v) OVER (ORDER BY k RANGE BETWEEN CURRENT ROW AND 1 FOLLOWING)
  FROM t ORDER BY k;
-- including the 0 spellings the page calls the same as CURRENT ROW
SELECT k, sum(v) OVER (ORDER BY k RANGE BETWEEN 0 PRECEDING AND CURRENT ROW)
  FROM t ORDER BY k;
-- nothing above changed the table
SELECT k, v FROM t ORDER BY k;
PRAGMA integrity_check;
