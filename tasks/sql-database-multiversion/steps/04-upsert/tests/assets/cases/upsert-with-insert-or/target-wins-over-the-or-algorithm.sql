-- PROFILE.md 4.5: a violation of the uniqueness constraint the conflict
-- target identifies is handled by the upsert clause -- "the insert is omitted
-- and either the DO NOTHING or DO UPDATE operation is performed instead" --
-- whatever OR algorithm the statement carries.
CREATE TABLE t(k INTEGER PRIMARY KEY, u TEXT UNIQUE, c TEXT);
INSERT INTO t VALUES(1,'x','one'),(2,'y','two');
-- OR REPLACE would delete the colliding row; the upsert clause updates it
-- instead, so u keeps its old value
INSERT OR REPLACE INTO t VALUES(1,'z','ONE')
  ON CONFLICT(k) DO UPDATE SET c=excluded.c;
SELECT k,u,c FROM t ORDER BY k;
SELECT count(*) FROM t;
-- OR IGNORE would skip the colliding row; the upsert clause updates it
INSERT OR IGNORE INTO t VALUES(2,'w','TWO')
  ON CONFLICT(k) DO UPDATE SET c=excluded.c;
SELECT k,u,c FROM t ORDER BY k;
-- OR ROLLBACK would end the transaction; the upsert clause updates the row
-- and the statement succeeds
INSERT OR ROLLBACK INTO t VALUES(1,'v','1v')
  ON CONFLICT(k) DO UPDATE SET c='rb';
SELECT k,u,c FROM t ORDER BY k;
PRAGMA integrity_check;
