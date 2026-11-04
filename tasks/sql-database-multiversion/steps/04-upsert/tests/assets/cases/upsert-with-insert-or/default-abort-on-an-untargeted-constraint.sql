-- PROFILE.md 4.5 with lang_conflict.html: "If no algorithm is specified
-- anywhere, the ABORT algorithm is used", which "aborts the current SQL
-- statement ... and backs out any changes made by the current SQL statement".
-- So the same rows that OR IGNORE skips reject the whole statement when no OR
-- prefix is written.
CREATE TABLE t(k INTEGER PRIMARY KEY, u TEXT UNIQUE, c TEXT NOT NULL,
               n INTEGER CHECK(n >= 0));
INSERT INTO t VALUES(1,'x','one',1),(2,'y','two',2);
-- an upsert on the targeted constraint succeeds and is visible
INSERT INTO t VALUES(1,'p','ONE',9) ON CONFLICT(k) DO UPDATE SET c=excluded.c, n=excluded.n;
SELECT k,u,c,n FROM t ORDER BY k;
-- a collision on the untargeted UNIQUE column
INSERT INTO t VALUES(3,'x','THREE',3) ON CONFLICT(k) DO UPDATE SET c=excluded.c;
SELECT k,u,c,n FROM t ORDER BY k;
-- a NOT NULL violation
INSERT INTO t VALUES(4,'w',NULL,4) ON CONFLICT(k) DO UPDATE SET c=excluded.c;
SELECT k,u,c,n FROM t ORDER BY k;
-- a CHECK violation
INSERT INTO t VALUES(5,'v','FIVE',-1) ON CONFLICT(k) DO UPDATE SET c=excluded.c;
SELECT k,u,c,n FROM t ORDER BY k;
-- ABORT "backs out any changes made by the current SQL statement", so the
-- rows written before the offending one in the same statement are gone too
INSERT INTO t VALUES(6,'a','SIX',6),(7,'x','SEVEN',7)
  ON CONFLICT(k) DO UPDATE SET c=excluded.c;
SELECT k,u,c,n FROM t ORDER BY k;
SELECT count(*) FROM t;
PRAGMA integrity_check;
