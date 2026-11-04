CREATE TABLE t(a INTEGER, b TEXT);
INSERT INTO t VALUES(1,'p'),(2,'q'),(3,'r'),(NULL,'s');
SELECT a FROM t WHERE a > 1 ORDER BY a;
SELECT b FROM t WHERE a IS NULL;
SELECT count(*) FROM t WHERE a;
SELECT count(*) FROM t WHERE 'english';
SELECT count(*) FROM t WHERE '1english';
