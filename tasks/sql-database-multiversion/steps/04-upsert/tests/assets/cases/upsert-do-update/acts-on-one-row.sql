-- lang_UPSERT.html: "the DO UPDATE clause acts only on the single row that
-- experienced the constraint error during INSERT. It is not necessary to
-- include a WHERE clause that restrictions the action to that one row."
-- (PROFILE.md 4.4)
CREATE TABLE t(k INTEGER PRIMARY KEY, c TEXT);
INSERT INTO t VALUES(1,'one'),(2,'two'),(3,'three');
INSERT INTO t VALUES(2,'TWO') ON CONFLICT(k) DO UPDATE SET c=excluded.c;
SELECT k,c FROM t ORDER BY k;
SELECT count(*) FROM t;
-- a row that conflicts with nothing is simply inserted, and no existing row
-- moves
INSERT INTO t VALUES(4,'four') ON CONFLICT(k) DO UPDATE SET c=excluded.c;
SELECT k,c FROM t ORDER BY k;
-- SET may assign the conflict-target column itself; the result is an ordinary
-- update of that one row
INSERT INTO t VALUES(1,'moved') ON CONFLICT(k) DO UPDATE SET k=9;
SELECT k,c FROM t ORDER BY k;
SELECT count(*) FROM t;
PRAGMA integrity_check;
