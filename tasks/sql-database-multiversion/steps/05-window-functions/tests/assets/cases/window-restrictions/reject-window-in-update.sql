-- windowfunctions.html: "Window functions may only appear in the result set and
-- in the ORDER BY clause of a SELECT statement" -- so neither half of an UPDATE
-- may carry one, including the DO UPDATE half of a step-4 upsert
-- (PROFILE.md 5.11, 4.4).  Each rejected statement leaves the table unchanged.
CREATE TABLE t(k INTEGER PRIMARY KEY, v INTEGER);
INSERT INTO t VALUES(1,10),(2,20),(3,30);
-- an ordinary UPDATE, and an ordinary upsert, both still work
UPDATE t SET v = v + 1 WHERE k = 1;
INSERT INTO t VALUES(2,99) ON CONFLICT(k) DO UPDATE SET v = excluded.v;
SELECT k, v FROM t ORDER BY k;
-- a window function in an UPDATE's SET list
UPDATE t SET v = row_number() OVER (ORDER BY k);
-- in an UPDATE's WHERE clause
UPDATE t SET v = 0 WHERE row_number() OVER (ORDER BY k) = 1;
-- in the SET list of a DO UPDATE
INSERT INTO t VALUES(1,5) ON CONFLICT(k) DO UPDATE SET v = rank() OVER (ORDER BY k);
-- in the trailing WHERE of a DO UPDATE
INSERT INTO t VALUES(1,5) ON CONFLICT(k) DO UPDATE SET v = excluded.v
  WHERE row_number() OVER (ORDER BY k) = 1;
-- nothing above changed the table
SELECT k, v FROM t ORDER BY k;
SELECT count(*) FROM t;
-- and the two positions the page does allow still work, so this case observes
-- the capability itself and not only a rejection that a release without window
-- functions would produce just as well (PROFILE.md 5.1, 5.4).
SELECT k, row_number() OVER (ORDER BY k) AS rn FROM t ORDER BY k;
SELECT k FROM t ORDER BY rank() OVER (ORDER BY v), k;
PRAGMA integrity_check;
