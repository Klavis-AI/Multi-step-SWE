CREATE TABLE t(a INTEGER, b INTEGER);
INSERT INTO t VALUES(1,10),(2,NULL),(NULL,30);
SELECT count(*) FROM t WHERE a = NULL;
SELECT count(*) FROM t WHERE a IS NULL;
SELECT count(a), count(b), count(*) FROM t;
SELECT sum(b), avg(b) FROM t;
SELECT max(a), min(a) FROM t;
SELECT a FROM t ORDER BY a;
