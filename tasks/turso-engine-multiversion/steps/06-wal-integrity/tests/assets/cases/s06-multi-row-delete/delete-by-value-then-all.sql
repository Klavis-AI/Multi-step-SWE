-- R06.14: a DELETE on a non-key column, then an unqualified DELETE that must
-- empty the table completely.
--
-- A predicate on a non-key column, then an unqualified DELETE that must empty
-- the table completely.
CREATE TABLE t(id INTEGER PRIMARY KEY, v TEXT);
INSERT INTO t VALUES(1,'a'),(2,'b'),(3,'c'),(4,'d'),(5,'e'),(6,'f'),(7,'g'),(8,'h');
DELETE FROM t WHERE v<>'c';
SELECT count(*) FROM t;
SELECT id,v FROM t ORDER BY id;
DELETE FROM t;
SELECT count(*), coalesce(sum(id),0) FROM t;
