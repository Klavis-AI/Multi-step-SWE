-- PROFILE.md 4.4: an upsert whose DO UPDATE would itself violate a constraint
-- is a statement that violates a constraint, so lang_conflict.html's default
-- ABORT algorithm applies -- the statement is rejected and the database is
-- left exactly as it was (step 1 section 7).
CREATE TABLE t(k INTEGER PRIMARY KEY, u TEXT UNIQUE, c TEXT NOT NULL,
               n INTEGER CHECK(n >= 0));
INSERT INTO t VALUES(1,'x','one',10),(2,'y','two',20);
-- an update that keeps every constraint satisfied is fine
INSERT INTO t VALUES(1,'z','ONE',1) ON CONFLICT(k) DO UPDATE SET c=excluded.c;
SELECT k,u,c,n FROM t ORDER BY k;
-- the DO UPDATE would duplicate the other row's UNIQUE column
INSERT INTO t VALUES(1,'z','ONE',1) ON CONFLICT(k) DO UPDATE SET u='y';
SELECT k,u,c,n FROM t ORDER BY k;
-- the DO UPDATE would violate NOT NULL
INSERT INTO t VALUES(1,'z','ONE',1) ON CONFLICT(k) DO UPDATE SET c=NULL;
SELECT k,u,c,n FROM t ORDER BY k;
-- the DO UPDATE would violate CHECK
INSERT INTO t VALUES(1,'z','ONE',1) ON CONFLICT(k) DO UPDATE SET n=-1;
SELECT k,u,c,n FROM t ORDER BY k;
PRAGMA integrity_check;
