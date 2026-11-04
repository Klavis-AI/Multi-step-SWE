-- windowfunctions.html: lag(expr) "returns the result of evaluating expression
-- expr against the previous row in the partition.  Or, if there is no previous
-- row ..., NULL"; with an offset, "the row offset rows before the current row
-- within the partition"; "If default is also provided, then it is returned
-- instead of NULL if the row identified by offset does not exist."  lead() is
-- the same forwards.  (PROFILE.md 5.4)
CREATE TABLE t(k INTEGER PRIMARY KEY, v INTEGER);
INSERT INTO t VALUES(1,10),(2,20),(3,30),(4,40),(5,50);
SELECT k, lag(v) OVER (ORDER BY k), lead(v) OVER (ORDER BY k) FROM t ORDER BY k;
-- the two- and three-argument forms, and offset 0 ("expr is evaluated against
-- the current row")
SELECT k, lag(v,2) OVER (ORDER BY k), lag(v,2,-1) OVER (ORDER BY k),
          lag(v,0) OVER (ORDER BY k)
  FROM t ORDER BY k;
SELECT k, lead(v,2) OVER (ORDER BY k), lead(v,2,-1) OVER (ORDER BY k),
          lead(v,0) OVER (ORDER BY k)
  FROM t ORDER BY k;
-- "Most built-in window functions ignore the frame-spec": a frame of one row
-- changes nothing for lag() or lead()
SELECT k, lag(v) OVER (ORDER BY k ROWS BETWEEN CURRENT ROW AND CURRENT ROW),
          lead(v) OVER (ORDER BY k ROWS BETWEEN CURRENT ROW AND CURRENT ROW)
  FROM t ORDER BY k;
-- the argument is an expression, and the previous row is the previous row of
-- the *partition*
CREATE TABLE p(k INTEGER PRIMARY KEY, g TEXT, v INTEGER);
INSERT INTO p VALUES(1,'x',1),(2,'x',2),(3,'y',3),(4,'y',4);
SELECT k, g, lag(v*10) OVER (PARTITION BY g ORDER BY k),
             lead(v*10) OVER (PARTITION BY g ORDER BY k)
  FROM p ORDER BY k;
