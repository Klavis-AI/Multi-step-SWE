-- R06.12: an UPDATE whose predicate matches no row is a no-op, not an error.
--
-- A predicate that matches nothing must leave the table exactly as it was and
-- must not be an error.
CREATE TABLE t(id INTEGER PRIMARY KEY, v TEXT);
INSERT INTO t VALUES(1,'one'),(2,'two');
UPDATE t SET v='never' WHERE id=99;
SELECT id,v FROM t ORDER BY id;
UPDATE t SET v='never' WHERE 0;
SELECT count(*) FROM t;
SELECT id,v FROM t ORDER BY id;
