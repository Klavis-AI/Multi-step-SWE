-- windowfunctions.html: first_value(expr) and last_value(expr) "calculate the
-- window frame for each row in the same way as an aggregate window function"
-- and return expr on the first and the last row of that frame; nth_value(expr,
-- N) returns "the value of expr evaluated against the row N of the window
-- frame", NULL when the frame has no such row.  These three are the exceptions
-- to "Most built-in window functions ignore the frame-spec".
-- (PROFILE.md 5.4)
CREATE TABLE t(k INTEGER PRIMARY KEY, v INTEGER);
INSERT INTO t VALUES(1,10),(2,20),(3,30),(4,40),(5,50);
-- the default frame: everything from the start of the partition to the current
-- row, so first_value is fixed, last_value walks, and nth_value(v,3) is NULL
-- until the frame is three rows long
SELECT k, first_value(v) OVER (ORDER BY k),
          last_value(v) OVER (ORDER BY k),
          nth_value(v,3) OVER (ORDER BY k)
  FROM t ORDER BY k;
-- the whole partition as the frame: all three are constant
SELECT k, first_value(v) OVER (ORDER BY k ROWS BETWEEN UNBOUNDED PRECEDING AND UNBOUNDED FOLLOWING),
          last_value(v) OVER (ORDER BY k ROWS BETWEEN UNBOUNDED PRECEDING AND UNBOUNDED FOLLOWING),
          nth_value(v,2) OVER (ORDER BY k ROWS BETWEEN UNBOUNDED PRECEDING AND UNBOUNDED FOLLOWING),
          nth_value(v,9) OVER (ORDER BY k ROWS BETWEEN UNBOUNDED PRECEDING AND UNBOUNDED FOLLOWING)
  FROM t ORDER BY k;
-- a sliding frame, and an expression rather than a bare column
SELECT k, first_value(v) OVER (ORDER BY k ROWS BETWEEN 1 PRECEDING AND 1 FOLLOWING),
          last_value(v) OVER (ORDER BY k ROWS BETWEEN 1 PRECEDING AND 1 FOLLOWING),
          nth_value(v,2) OVER (ORDER BY k ROWS BETWEEN 1 PRECEDING AND 1 FOLLOWING),
          nth_value(v,3) OVER (ORDER BY k ROWS BETWEEN 1 PRECEDING AND 1 FOLLOWING),
          first_value(v*2) OVER (ORDER BY k ROWS BETWEEN 1 PRECEDING AND 1 FOLLOWING)
  FROM t ORDER BY k;
-- and per partition
CREATE TABLE p(k INTEGER PRIMARY KEY, g TEXT, v INTEGER);
INSERT INTO p VALUES(1,'x',1),(2,'x',2),(3,'y',3),(4,'y',4),(5,'y',5);
SELECT k, g, first_value(v) OVER (PARTITION BY g ORDER BY k
                                  ROWS BETWEEN UNBOUNDED PRECEDING AND UNBOUNDED FOLLOWING),
             last_value(v) OVER (PARTITION BY g ORDER BY k
                                  ROWS BETWEEN UNBOUNDED PRECEDING AND UNBOUNDED FOLLOWING)
  FROM p ORDER BY k;
