CREATE TABLE t(a INTEGER, b TEXT);
INSERT INTO t VALUES(2,'q'),(1,'r'),(3,'p'),(1,'a');
SELECT a, b FROM t ORDER BY a, b;
SELECT a, b FROM t ORDER BY a DESC, b ASC;
SELECT a, b FROM t ORDER BY 2, 1;
SELECT a AS num, b AS txt FROM t ORDER BY txt, num;
SELECT a * 10 AS scaled FROM t ORDER BY a * -1, a;
