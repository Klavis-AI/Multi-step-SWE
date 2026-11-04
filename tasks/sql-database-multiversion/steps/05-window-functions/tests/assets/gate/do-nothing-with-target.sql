-- lang_UPSERT.html: "The conflict target specifies a specific uniqueness
-- constraint that will trigger the upsert", and the upsert runs only "If the
-- insert operation would cause the uniqueness constraint identified by the
-- conflict-target clause to fail".  A collision on a constraint the target
-- does not identify is left to the statement's own conflict algorithm, which
-- here is the default ABORT (PROFILE.md 4.2, 4.5, lang_conflict.html).
CREATE TABLE t(k INTEGER PRIMARY KEY, u TEXT UNIQUE, v TEXT);
INSERT INTO t VALUES(1,'x','one'),(2,'y','two');
-- a row that collides with nothing is inserted by the upsert like any other
INSERT INTO t VALUES(9,'w','nine') ON CONFLICT(k) DO NOTHING;
SELECT k,u,v FROM t ORDER BY k;
-- the target names the PRIMARY KEY, and a PRIMARY KEY collision is skipped
INSERT INTO t VALUES(1,'z','ONE') ON CONFLICT(k) DO NOTHING;
SELECT k,u,v FROM t ORDER BY k;
-- a collision on the UNIQUE column is not that constraint: the statement is
-- rejected and the table is left exactly as it was
INSERT INTO t VALUES(3,'x','THREE') ON CONFLICT(k) DO NOTHING;
SELECT k,u,v FROM t ORDER BY k;
-- naming the UNIQUE column instead makes the same row a no-op
INSERT INTO t VALUES(4,'x','FOUR') ON CONFLICT(u) DO NOTHING;
SELECT k,u,v FROM t ORDER BY k;
PRAGMA integrity_check;
