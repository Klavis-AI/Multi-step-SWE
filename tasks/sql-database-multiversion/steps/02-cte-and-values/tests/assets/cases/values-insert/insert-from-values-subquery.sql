-- lang_insert.html's second form takes a SELECT; a VALUES clause is one, so it
-- may reach an INSERT through a FROM subquery as well as directly.
CREATE TABLE t(a INTEGER PRIMARY KEY, b TEXT);
INSERT INTO t SELECT * FROM (VALUES(1,'p'),(2,'q'));
SELECT a, b FROM t ORDER BY a;
INSERT INTO t(a, b) SELECT * FROM (VALUES(3,'r'));
SELECT count(*), sum(a) FROM t;
INSERT INTO t(b) SELECT * FROM (VALUES('s'));
SELECT b FROM t WHERE b = 's';
