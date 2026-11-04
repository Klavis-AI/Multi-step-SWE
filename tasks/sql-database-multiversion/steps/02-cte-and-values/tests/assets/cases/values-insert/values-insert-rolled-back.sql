-- An INSERT whose source is a VALUES query obeys step 1 section 7's
-- transaction and savepoint rules exactly as any other INSERT does.
CREATE TABLE t(a INTEGER PRIMARY KEY, b TEXT);
INSERT INTO t VALUES(1,'p');
BEGIN;
INSERT INTO t SELECT * FROM (VALUES(2,'q'),(3,'r'));
SELECT count(*) FROM t;
ROLLBACK;
SELECT count(*) FROM t;
SAVEPOINT s1;
INSERT INTO t SELECT * FROM (VALUES(4,'s'));
SELECT count(*) FROM t;
ROLLBACK TO s1;
SELECT count(*) FROM t;
RELEASE s1;
BEGIN;
INSERT INTO t SELECT * FROM (VALUES(5,'t'));
COMMIT;
SELECT a, b FROM t ORDER BY a;
