CREATE TABLE t(a INTEGER PRIMARY KEY, b TEXT);
INSERT INTO t VALUES(1,'p'),(2,'q'),(3,'r'),(4,'s');
-- a common table expression joined to a real table
WITH s(k) AS (SELECT a FROM t WHERE a % 2 = 0)
  SELECT t.a, t.b FROM t JOIN s ON t.a = s.k ORDER BY t.a;
-- aggregated over
WITH s(k, v) AS (SELECT a, b FROM t) SELECT count(k), sum(k), min(v), max(v) FROM s;
-- ORDER BY and LIMIT apply to the statement, not to the CTE
WITH s(k) AS (SELECT a FROM t) SELECT k FROM s ORDER BY k DESC LIMIT 2;
