-- windowfunctions.html, Introduction: "Window functions may only appear in the
-- result set and in the ORDER BY clause of a SELECT statement."  Each rejected
-- statement leaves the database exactly as it was (PROFILE.md 5.11,
-- TEST_INTERFACE.md 2.3).
CREATE TABLE t(k INTEGER PRIMARY KEY, g TEXT, v INTEGER);
INSERT INTO t VALUES(1,'a',10),(2,'a',20),(3,'b',30);
-- the two places the page allows
SELECT k, row_number() OVER (ORDER BY k) AS rn FROM t ORDER BY k;
SELECT k FROM t ORDER BY row_number() OVER (ORDER BY v DESC);
-- a window function in a WHERE clause
SELECT k FROM t WHERE row_number() OVER (ORDER BY k) = 1 ORDER BY k;
-- in a GROUP BY clause
SELECT g FROM t GROUP BY row_number() OVER (ORDER BY v) ORDER BY g;
-- in a HAVING clause
SELECT g FROM t GROUP BY g HAVING count(*) OVER () > 0 ORDER BY g;
-- in the WHERE clause of a DELETE
DELETE FROM t WHERE row_number() OVER (ORDER BY k) = 1;
-- nothing above changed the table
SELECT k, g, v FROM t ORDER BY k;
SELECT count(*) FROM t;
PRAGMA integrity_check;
