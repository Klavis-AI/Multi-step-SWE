-- PROFILE.md 4.5: a violation of anything the conflict target does not
-- identify is handled by the statement's own conflict algorithm, exactly as it
-- is on an INSERT with no upsert clause (lang_conflict.html).
CREATE TABLE t(k INTEGER PRIMARY KEY, u TEXT UNIQUE, c TEXT NOT NULL);
INSERT INTO t VALUES(1,'x','one'),(2,'y','two');
-- OR IGNORE "skips the one row that contains the constraint violation and
-- continues processing subsequent rows": row 1 collides on the targeted
-- PRIMARY KEY and is updated, the second row collides on the untargeted
-- UNIQUE column and is skipped, the third violates NOT NULL and is skipped,
-- and the fourth is inserted.  No error is returned.
INSERT OR IGNORE INTO t VALUES(1,'p','ONE'),(3,'x','THREE'),(4,'q',NULL),(5,'w','FIVE')
  ON CONFLICT(k) DO UPDATE SET c=excluded.c;
SELECT k,u,c FROM t ORDER BY k;
-- OR REPLACE "deletes pre-existing rows that are causing the constraint
-- violation prior to inserting", so the row colliding on the untargeted UNIQUE
-- column displaces the row that held it
INSERT OR REPLACE INTO t VALUES(6,'x','SIX')
  ON CONFLICT(k) DO UPDATE SET c=excluded.c;
SELECT k,u,c FROM t ORDER BY k;
PRAGMA integrity_check;
