-- A common table expression over a table carrying an ordinary, a UNIQUE and a
-- partial index answers exactly as it does without them.
CREATE TABLE t(a INTEGER, b TEXT);
CREATE INDEX ia ON t(a);
CREATE UNIQUE INDEX ib ON t(b);
CREATE INDEX ipart ON t(a) WHERE b > 'q';
INSERT INTO t VALUES(3,'r'),(1,'p'),(2,'q');
WITH s(k, v) AS (SELECT a, b FROM t WHERE a >= 2) SELECT k, v FROM s ORDER BY k;
WITH s(k, v) AS (SELECT a, b FROM t) SELECT v FROM s WHERE v = 'q';
WITH s(k) AS (SELECT a FROM t) SELECT count(k), sum(k) FROM s;
WITH s(k, v) AS (SELECT a, b FROM t WHERE b > 'q') SELECT k, v FROM s ORDER BY k;
