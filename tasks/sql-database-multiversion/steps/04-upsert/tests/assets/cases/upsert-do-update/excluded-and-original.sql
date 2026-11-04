-- lang_UPSERT.html: "Column names in the expressions of a DO UPDATE refer to
-- the original unchanged value of the column, before the attempted INSERT. To
-- use the value that would have been inserted had the constraint not failed,
-- add the special 'excluded.' table qualifier to the column name."
-- (PROFILE.md 4.4)
CREATE TABLE t(k INTEGER PRIMARY KEY, c TEXT, n INTEGER, d TEXT DEFAULT 'dflt');
INSERT INTO t VALUES(1,'one',10,'D1');
-- the unqualified name is the original value, the excluded. name is the one
-- that would have been inserted, and both can appear in the same expression
INSERT INTO t(k,c,n,d) VALUES(1,'ONE',5,'D2')
  ON CONFLICT(k) DO UPDATE SET c=c||'/'||excluded.c, n=n+excluded.n;
SELECT k,c,n,d FROM t ORDER BY k;
-- the table name and the AS alias are two more spellings of the original value
INSERT INTO t(k,c,n) VALUES(1,'X',1)
  ON CONFLICT(k) DO UPDATE SET n=t.n+excluded.n;
SELECT k,c,n,d FROM t ORDER BY k;
INSERT INTO t AS old(k,c,n) VALUES(1,'X',2)
  ON CONFLICT(k) DO UPDATE SET n=old.n+excluded.n;
SELECT k,c,n,d FROM t ORDER BY k;
-- lang_insert.html: "Table columns that do not appear in the column list are
-- populated with the default column value ..., or with NULL if no default
-- value is specified", so excluded.d for a column the INSERT did not name is
-- that default
INSERT INTO t(k,c) VALUES(1,'Y')
  ON CONFLICT(k) DO UPDATE SET d=excluded.d;
SELECT k,c,n,d FROM t ORDER BY k;
-- and NULL where the column has no DEFAULT
CREATE TABLE u(k INTEGER PRIMARY KEY, e TEXT);
INSERT INTO u VALUES(1,'kept');
INSERT INTO u(k) VALUES(1) ON CONFLICT(k) DO UPDATE SET e=excluded.e;
SELECT k, typeof(e), coalesce(e,'<null>') FROM u ORDER BY k;
PRAGMA integrity_check;
