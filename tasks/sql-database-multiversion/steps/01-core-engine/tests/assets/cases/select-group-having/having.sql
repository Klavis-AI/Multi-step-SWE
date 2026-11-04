CREATE TABLE t(g TEXT, v INTEGER);
INSERT INTO t VALUES('a',1),('a',2),('b',10),('c',3),('c',4),('c',5);
SELECT g, count(*) FROM t GROUP BY g HAVING count(*) > 1 ORDER BY g;
SELECT g FROM t GROUP BY g HAVING sum(v) >= 10 ORDER BY g;
SELECT g, sum(v) FROM t WHERE v > 1 GROUP BY g HAVING g <> 'b' ORDER BY g;
