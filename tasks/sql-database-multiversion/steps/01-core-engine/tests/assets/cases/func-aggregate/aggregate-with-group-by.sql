CREATE TABLE t(g TEXT, v INTEGER);
INSERT INTO t VALUES('a',1),('a',1),('a',3),('b',10);
SELECT g, count(*), count(DISTINCT v), sum(v), max(v), min(v) FROM t GROUP BY g ORDER BY g;
SELECT count(*) FROM t GROUP BY g HAVING count(DISTINCT v) = 1;
