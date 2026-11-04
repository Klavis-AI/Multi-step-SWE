-- lang_UPSERT.html: "The only use for the WHERE clause at the end of the DO
-- UPDATE is to optionally change the DO UPDATE into a no-op depending on the
-- original and/or new values."  When it is not true the row is neither
-- inserted nor updated and the statement still succeeds (PROFILE.md 4.4).
CREATE TABLE t(k INTEGER PRIMARY KEY, n INTEGER, c TEXT);
INSERT INTO t VALUES(1,10,'one'),(2,20,'two');
-- the WHERE clause is true, so the update happens
INSERT INTO t VALUES(1,30,'ONE')
  ON CONFLICT(k) DO UPDATE SET n=excluded.n, c=excluded.c
  WHERE excluded.n > t.n;
SELECT k,n,c FROM t ORDER BY k;
-- the WHERE clause is false, so the whole upsert is a no-op for that row and
-- the statement still succeeds
INSERT INTO t VALUES(1,5,'ignored')
  ON CONFLICT(k) DO UPDATE SET n=excluded.n, c=excluded.c
  WHERE excluded.n > t.n;
SELECT k,n,c FROM t ORDER BY k;
SELECT count(*) FROM t;
-- a WHERE clause that is never true never changes anything, and a row that
-- does not conflict is still inserted by the same statement
INSERT INTO t VALUES(2,1,'no'),(3,3,'three')
  ON CONFLICT(k) DO UPDATE SET n=excluded.n WHERE 0;
SELECT k,n,c FROM t ORDER BY k;
-- a WHERE clause that is always true behaves like no WHERE clause at all
INSERT INTO t VALUES(2,99,'TWO')
  ON CONFLICT(k) DO UPDATE SET n=excluded.n, c=excluded.c WHERE 1;
SELECT k,n,c FROM t ORDER BY k;
PRAGMA integrity_check;
