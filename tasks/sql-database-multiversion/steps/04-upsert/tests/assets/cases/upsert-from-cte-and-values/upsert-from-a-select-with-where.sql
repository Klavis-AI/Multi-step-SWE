-- lang_insert.html: "To avoid a parsing ambiguity, the SELECT statement should
-- always contain a WHERE clause ... if the upsert-clause is present."  The
-- WHERE-bearing form is what PROFILE.md 4.1 requires; PROFILE.md 4.9 leaves
-- the ambiguous form unscored in both directions, so it is not written here.
CREATE TABLE t(k INTEGER PRIMARY KEY, c TEXT, n INTEGER);
CREATE TABLE s(k INTEGER, c TEXT, n INTEGER);
CREATE TABLE lk(k INTEGER, tag TEXT);
INSERT INTO t VALUES(1,'one',1),(2,'two',2);
INSERT INTO s VALUES(1,'ONE',10),(3,'THREE',30),(4,'FOUR',40);
INSERT INTO lk VALUES(1,'a'),(3,'c');
-- a WHERE clause that selects every source row
INSERT INTO t(k,c,n) SELECT k,c,n FROM s WHERE 1
  ON CONFLICT(k) DO UPDATE SET c=excluded.c, n=excluded.n;
SELECT k,c,n FROM t ORDER BY k;
-- a WHERE clause that selects some of them
INSERT INTO t(k,c,n) SELECT k,'again',n*2 FROM s WHERE k > 3
  ON CONFLICT(k) DO UPDATE SET c=excluded.c, n=excluded.n;
SELECT k,c,n FROM t ORDER BY k;
-- a SELECT with a join, still ending in a WHERE clause before the upsert
-- clause: step 1 section 1's join syntax is unchanged
INSERT INTO t(k,c,n)
  SELECT s.k+100, s.c||'-'||lk.tag, s.n FROM s JOIN lk ON lk.k = s.k WHERE s.n > 0
  ON CONFLICT(k) DO UPDATE SET c=excluded.c;
SELECT k,c,n FROM t ORDER BY k;
PRAGMA integrity_check;
