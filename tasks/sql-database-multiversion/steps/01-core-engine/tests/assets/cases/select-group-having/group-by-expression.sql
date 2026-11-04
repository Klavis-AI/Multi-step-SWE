CREATE TABLE t(a INTEGER);
INSERT INTO t VALUES(1),(2),(3),(4),(5),(6);
SELECT a % 3 AS m, count(*) FROM t GROUP BY a % 3 ORDER BY m;
SELECT count(*) FROM t GROUP BY a % 2 ORDER BY a % 2;
