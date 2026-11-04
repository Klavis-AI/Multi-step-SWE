-- lang_createindex.html: "If the UNIQUE keyword appears between CREATE and
-- INDEX then duplicate index entries are not allowed.  Any attempt to insert
-- a duplicate entry will result in an error.  For the purposes of unique
-- indices, all NULL values are considered to different from all other NULL
-- values and are thus unique."  expridx.html allows an expression as the
-- index key of such a statement -- what it forbids is an expression inside a
-- UNIQUE constraint of a CREATE TABLE.
CREATE TABLE t(id INTEGER PRIMARY KEY, a INTEGER, b TEXT);
CREATE UNIQUE INDEX tu ON t(abs(a));
INSERT INTO t VALUES(1,-5,'p'),(2,3,'q');
SELECT count(id) FROM t;
-- a row whose indexed expression duplicates an existing one is rejected, and
-- the table is left exactly as it was (PROFILE.md 7, lang_conflict.html: the
-- default conflict algorithm is ABORT)
INSERT INTO t VALUES(3,5,'r');
SELECT count(id) FROM t;
SELECT id, a FROM t ORDER BY id;
-- an UPDATE that would create a duplicate is rejected the same way
UPDATE t SET a = -3 WHERE id = 1;
SELECT id, a FROM t ORDER BY id;
-- a row whose indexed expression is new is accepted
INSERT INTO t VALUES(4,-9,'s');
SELECT id, a FROM t ORDER BY id;
-- "all NULL values are considered to different from all other NULL values":
-- abs(NULL) is NULL, so two such rows both fit in a unique index
INSERT INTO t VALUES(5,NULL,'t');
INSERT INTO t VALUES(6,NULL,'u');
SELECT count(id) FROM t;
-- INSERT OR IGNORE skips the conflicting row instead (PROFILE.md 1)
INSERT OR IGNORE INTO t VALUES(7,9,'v');
SELECT count(id) FROM t;
SELECT changes();
PRAGMA integrity_check;
