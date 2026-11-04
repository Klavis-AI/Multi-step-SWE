-- lang_with.html: a WITH clause may be prepended to a DELETE statement.
CREATE TABLE t(a INTEGER PRIMARY KEY, b TEXT);
INSERT INTO t VALUES(1,'p'),(2,'q'),(3,'r'),(4,'s');
WITH s(k) AS (SELECT a FROM t WHERE a % 2 = 0) DELETE FROM t WHERE a IN (SELECT k FROM s);
SELECT a, b FROM t ORDER BY a;
SELECT changes();
WITH gone(k) AS (VALUES(3)) DELETE FROM t WHERE a IN (SELECT k FROM gone);
SELECT a, b FROM t ORDER BY a;
