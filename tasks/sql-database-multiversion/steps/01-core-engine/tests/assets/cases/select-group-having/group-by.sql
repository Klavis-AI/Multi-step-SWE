CREATE TABLE t(g TEXT, v INTEGER);
INSERT INTO t VALUES('a',1),('a',2),('b',10),(NULL,5),(NULL,7);
SELECT g, count(*), sum(v) FROM t GROUP BY g ORDER BY g;
SELECT count(*) FROM t;
SELECT count(*), sum(v) FROM t;
DELETE FROM t;
SELECT count(*), sum(v) IS NULL, total(v) FROM t;
