-- PROFILE.md 4.8: ROLLBACK TO a savepoint undoes an upsert, and every index on
-- the table -- including a UNIQUE index and one on an expression -- stays
-- consistent through it (step 1 section 7, step 3 section 3.4).  Each upsert
-- below is written so that at most one uniqueness constraint can be violated.
CREATE TABLE t(k INTEGER PRIMARY KEY, u TEXT UNIQUE, n INTEGER);
CREATE INDEX tabs ON t(abs(n));
INSERT INTO t VALUES(1,'x',-5),(2,'y',3);
SAVEPOINT s1;
INSERT INTO t VALUES(11,'x',-9) ON CONFLICT(u) DO UPDATE SET n=excluded.n;
INSERT INTO t VALUES(3,'z',7) ON CONFLICT(u) DO UPDATE SET n=excluded.n;
SELECT k,u,n FROM t ORDER BY k;
SELECT count(*) FROM t WHERE abs(n)=9;
SELECT count(*) FROM t WHERE abs(n)=5;
SELECT k FROM t WHERE abs(n)=7 ORDER BY k;
ROLLBACK TO s1;
RELEASE s1;
SELECT k,u,n FROM t ORDER BY k;
SELECT count(*) FROM t WHERE abs(n)=9;
SELECT count(*) FROM t WHERE abs(n)=5;
SELECT count(*) FROM t;
-- and again outside any savepoint, so the expression index tracks the
-- committed state
INSERT INTO t VALUES(11,'x',-9) ON CONFLICT(u) DO UPDATE SET n=excluded.n;
SELECT count(*) FROM t WHERE abs(n)=9;
SELECT count(*) FROM t WHERE abs(n)=5;
PRAGMA integrity_check;
