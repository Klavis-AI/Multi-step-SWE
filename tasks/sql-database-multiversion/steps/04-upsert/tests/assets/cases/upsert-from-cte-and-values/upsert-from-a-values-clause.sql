-- Step 2 section 2.2 makes VALUES a query in its own right, usable as a CTE
-- body and as a FROM subquery; PROFILE.md 4.1 keeps the upsert clause at the
-- end of the INSERT either way.
CREATE TABLE t(k INTEGER PRIMARY KEY, c TEXT);
INSERT INTO t VALUES(1,'one'),(2,'two');
-- a VALUES-bodied CTE as the source
WITH v(k,c) AS (VALUES(2,'TWO'),(3,'THREE'))
INSERT INTO t(k,c) SELECT k,c FROM v WHERE 1
  ON CONFLICT(k) DO UPDATE SET c=excluded.c||'!';
SELECT k,c FROM t ORDER BY k;
-- a VALUES clause as a FROM subquery.  Step 2 section 2.5 leaves the result
-- column names of a bare VALUES clause unnamed and out of scope, so those
-- columns are reached here by *, which is one of the three routes that section
-- allows.
INSERT INTO t(k,c) SELECT * FROM (VALUES(1,'ONE'),(4,'FOUR')) WHERE 1
  ON CONFLICT(k) DO UPDATE SET c=excluded.c;
SELECT k,c FROM t ORDER BY k;
-- and the plain VALUES list of step 1, which needs no WHERE clause because
-- there is no SELECT to be ambiguous with
INSERT INTO t(k,c) VALUES(4,'four'),(5,'five')
  ON CONFLICT(k) DO UPDATE SET c=excluded.c;
SELECT k,c FROM t ORDER BY k;
PRAGMA integrity_check;
