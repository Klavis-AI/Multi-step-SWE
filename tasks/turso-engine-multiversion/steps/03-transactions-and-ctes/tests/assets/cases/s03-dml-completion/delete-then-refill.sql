-- R03.14: a table emptied by DELETE is still a table -- its sqlite_schema row
-- survives, it answers a query with nothing, and it refills with the rowid
-- stock sqlite3 hands out after an emptied table, which last_insert_rowid()
-- reports.
-- A table emptied one row at a time is still a table: its schema row survives,
-- it answers a query with nothing, and rows inserted afterwards are found
-- again. The rowid handed out after the table was emptied is recorded too, so
-- an engine that restarts its rowid counter and one that continues it are told
-- apart.
CREATE TABLE t(id INTEGER PRIMARY KEY, s TEXT);
INSERT INTO t VALUES(1,'one');
INSERT INTO t VALUES(2,'two');
INSERT INTO t VALUES(3,'three');
SELECT count(*), min(id), max(id) FROM t;
DELETE FROM t WHERE id = 1;
DELETE FROM t WHERE id = 2;
DELETE FROM t WHERE id = 3;
SELECT count(*), coalesce(max(id), -1) FROM t;
SELECT type, name FROM sqlite_schema ORDER BY name;
INSERT INTO t(s) VALUES('after');
SELECT changes(), last_insert_rowid();
INSERT INTO t(s) VALUES('after two');
SELECT changes(), last_insert_rowid();
SELECT id, s FROM t ORDER BY id;
SELECT count(*), min(id), max(id) FROM t;
