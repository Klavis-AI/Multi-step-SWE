CREATE TABLE t(a INTEGER, b TEXT);
INSERT INTO t VALUES(1,'p'),(2,'q'),(3,'r');
SELECT s.a, s.b FROM (SELECT a, b FROM t WHERE a >= 2) AS s ORDER BY s.a;
SELECT n FROM (SELECT a * 2 AS n FROM t) ORDER BY n;
SELECT x.a, y.a FROM (SELECT a FROM t) AS x JOIN (SELECT a FROM t) AS y ON x.a = y.a ORDER BY x.a;
