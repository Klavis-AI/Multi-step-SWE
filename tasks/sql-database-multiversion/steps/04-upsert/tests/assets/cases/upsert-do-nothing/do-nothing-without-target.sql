-- lang_UPSERT.html: the conflict target "is optional for DO NOTHING", and
-- "A DO NOTHING upsert without a conflict target works the same as an INSERT
-- OR IGNORE."  With no target the clause therefore covers every uniqueness
-- constraint of the table (PROFILE.md 4.3).
CREATE TABLE t(k INTEGER PRIMARY KEY, u TEXT UNIQUE, v TEXT);
INSERT INTO t VALUES(1,'x','one'),(2,'y','two');
-- a row that collides on the PRIMARY KEY is skipped and the statement succeeds
INSERT INTO t VALUES(1,'z','ONE') ON CONFLICT DO NOTHING;
SELECT k,u,v FROM t ORDER BY k;
-- so is one that collides on the UNIQUE column
INSERT INTO t VALUES(3,'x','THREE') ON CONFLICT DO NOTHING;
SELECT k,u,v FROM t ORDER BY k;
-- a row that collides with nothing is inserted like any other
INSERT INTO t VALUES(4,'w','FOUR') ON CONFLICT DO NOTHING;
SELECT k,u,v FROM t ORDER BY k;
-- "In the case of a multi-row insert, this decision is made separately for
-- each row of the insert": the first two rows below are skipped and the third
-- is inserted, all by one statement.
INSERT INTO t VALUES(1,'p','P'),(5,'y','Q'),(6,'r','R') ON CONFLICT DO NOTHING;
SELECT k,u,v FROM t ORDER BY k;
PRAGMA integrity_check;
