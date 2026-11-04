-- R06.10: an unqualified UPDATE over every row, with the new value an
-- expression over the row's existing values.
--
-- An unqualified UPDATE touches every row and the new value is computed from
-- the old one, so the engine must read each row before writing it.
CREATE TABLE t(id INTEGER PRIMARY KEY, n INTEGER, s TEXT);
INSERT INTO t VALUES(1,5,'aa'),(2,7,'bb'),(3,11,'cc');
UPDATE t SET n=n*2+1, s=s||'!';
SELECT id,n,s FROM t ORDER BY id;
SELECT sum(n) FROM t;
