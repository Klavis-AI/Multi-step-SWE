-- PROFILE.md 4.8: an upsert keeps every index on the table consistent,
-- including a non-unique index on an expression (step 3 section 3.4's second
-- consequence), and the answers do not depend on the index existing.
CREATE TABLE t(k INTEGER PRIMARY KEY, a INTEGER, b TEXT);
INSERT INTO t VALUES(1,-5,'apple'),(2,3,'pear'),(3,-1,'plum');
-- the answers before the index exists
SELECT k FROM t WHERE abs(a)=5 ORDER BY k;
SELECT k FROM t WHERE lower(b)='pear' ORDER BY k;
CREATE INDEX tabs ON t(abs(a));
CREATE INDEX tlow ON t(lower(b));
-- the same answers after it exists
SELECT k FROM t WHERE abs(a)=5 ORDER BY k;
SELECT k FROM t WHERE lower(b)='pear' ORDER BY k;
-- an upsert that updates a row through one index and inserts through another
INSERT INTO t(k,a,b) VALUES(2,-9,'PEACH'),(4,7,'fig')
  ON CONFLICT(k) DO UPDATE SET a=excluded.a, b=excluded.b;
SELECT k,a,b FROM t ORDER BY k;
SELECT k FROM t WHERE abs(a)=9 ORDER BY k;
SELECT count(*) FROM t WHERE abs(a)=3;
SELECT k FROM t WHERE lower(b)='peach' ORDER BY k;
SELECT k FROM t WHERE lower(b)='fig' ORDER BY k;
-- dropping the indexes changes no answer
DROP INDEX tabs;
DROP INDEX IF EXISTS tlow;
SELECT k FROM t WHERE abs(a)=9 ORDER BY k;
SELECT k FROM t WHERE lower(b)='peach' ORDER BY k;
SELECT k,a,b FROM t ORDER BY k;
PRAGMA integrity_check;
