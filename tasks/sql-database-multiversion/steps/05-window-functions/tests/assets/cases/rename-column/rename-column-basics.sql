-- lang_altertable.html: "The RENAME COLUMN TO syntax changes the column-name of
-- table table-name into new-column-name."  PROFILE.md 5.9: afterwards the
-- column is known by its new name in SELECT result columns, in WHERE, in ORDER
-- BY, in INSERT and UPDATE column lists and as a step-4 conflict target, and by
-- its old name nowhere; PRAGMA table_info reports the new name with the same
-- cid, type, notnull, dflt_value and pk.
CREATE TABLE t(k INTEGER PRIMARY KEY, old TEXT NOT NULL DEFAULT 'd', n INTEGER);
INSERT INTO t(k,old,n) VALUES(1,'x',10),(2,'y',20);
PRAGMA table_info(t);
ALTER TABLE t RENAME COLUMN old TO new;
PRAGMA table_info(t);
-- the new name works in a result column, a WHERE clause and an ORDER BY
SELECT k, new FROM t ORDER BY k;
SELECT k FROM t WHERE new = 'x';
SELECT new FROM t ORDER BY new DESC;
-- and in INSERT and UPDATE column lists
INSERT INTO t(k,new,n) VALUES(3,'z',30);
UPDATE t SET new = 'Z' WHERE k = 3;
SELECT k, new, n FROM t ORDER BY k;
-- the default survives, so an INSERT that omits the column still gets it
INSERT INTO t(k,n) VALUES(4,40);
SELECT k, new, n FROM t ORDER BY k;
PRAGMA integrity_check;
-- PROFILE.md 5.9: the column is known by its new name "as a step-4 conflict
-- target" too -- the uniqueness constraint behind it is the same one, so the
-- upsert of step 4 section 4.2 binds to it under the new name.
CREATE TABLE u(a INTEGER PRIMARY KEY, old TEXT UNIQUE, n INTEGER);
INSERT INTO u VALUES(1,'p',1),(2,'q',2);
ALTER TABLE u RENAME COLUMN old TO new;
INSERT INTO u(a,new,n) VALUES(3,'p',30)
  ON CONFLICT(new) DO UPDATE SET n = excluded.n;
SELECT a, new, n FROM u ORDER BY a;
INSERT INTO u(a,new,n) VALUES(4,'q',99)
  ON CONFLICT(new) DO UPDATE SET n = u.n + 1;
SELECT a, new, n FROM u ORDER BY a;
PRAGMA integrity_check;
