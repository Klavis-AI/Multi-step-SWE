-- windowfunctions.html, Frame Specifications: "If the frame type is RANGE,
-- then rows with the same values for all ORDER BY expressions are considered
-- 'peers'. ... Rows that are peers always have the same window frames", and
-- "CURRENT ROW ... For RANGE frame types, all peers of the current row are
-- also included in the window frame, regardless of whether CURRENT ROW is used
-- as the starting or ending frame boundary."  (PROFILE.md 5.3)
CREATE TABLE p(k INTEGER PRIMARY KEY, s TEXT, v INTEGER);
INSERT INTO p VALUES(1,'a',1),(2,'a',2),(3,'b',4),(4,'b',8),(5,'c',16);
-- CURRENT ROW at both ends of a RANGE frame is the current row's whole peer
-- group, so the answer is the same for both peers of each group
SELECT k, s, sum(v) OVER (ORDER BY s RANGE BETWEEN CURRENT ROW AND CURRENT ROW)
  FROM p ORDER BY k;
-- and it agrees, group for group, with the ordinary aggregate under GROUP BY
SELECT s, sum(v) FROM p GROUP BY s ORDER BY s;
-- CURRENT ROW as the starting boundary still includes the peers
SELECT k, s, sum(v) OVER (ORDER BY s RANGE BETWEEN CURRENT ROW AND UNBOUNDED FOLLOWING)
  FROM p ORDER BY k;
-- and as the ending boundary
SELECT k, s, sum(v) OVER (ORDER BY s RANGE BETWEEN UNBOUNDED PRECEDING AND CURRENT ROW)
  FROM p ORDER BY k;
-- with no ORDER BY term at all "all rows are peers", so the frame is the whole
-- partition whichever RANGE boundaries are written
SELECT k, sum(v) OVER (RANGE BETWEEN UNBOUNDED PRECEDING AND CURRENT ROW),
          sum(v) OVER (RANGE BETWEEN CURRENT ROW AND UNBOUNDED FOLLOWING)
  FROM p ORDER BY k;
