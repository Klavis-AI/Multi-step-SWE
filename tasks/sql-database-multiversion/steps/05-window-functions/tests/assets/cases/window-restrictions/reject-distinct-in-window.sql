-- windowfunctions.html, Introduction: "Unlike ordinary functions, window
-- functions cannot use the DISTINCT keyword."  (PROFILE.md 5.11)
CREATE TABLE t(k INTEGER PRIMARY KEY, v INTEGER);
INSERT INTO t VALUES(1,10),(2,10),(3,20);
-- DISTINCT is fine in the plain aggregate, and fine as SELECT DISTINCT
SELECT count(DISTINCT v) FROM t;
SELECT DISTINCT count(*) OVER () FROM t;
-- but not inside a window function invocation
SELECT k, count(DISTINCT v) OVER (ORDER BY k) FROM t ORDER BY k;
SELECT k, sum(DISTINCT v) OVER (ORDER BY k) FROM t ORDER BY k;
SELECT k, group_concat(DISTINCT v) OVER (ORDER BY k) FROM t ORDER BY k;
-- nothing above changed the table
SELECT k, v FROM t ORDER BY k;
PRAGMA integrity_check;
