-- A common table expression is visible to the subqueries of the statement
-- that defines it.
CREATE TABLE t(a INTEGER, b TEXT);
INSERT INTO t VALUES(1,'p'),(2,'q'),(3,'r');
WITH s(k) AS (SELECT a FROM t WHERE a >= 2)
  SELECT a FROM t WHERE a IN (SELECT k FROM s) ORDER BY a;
WITH s(k) AS (SELECT a FROM t)
  SELECT a FROM t WHERE a NOT IN (SELECT k FROM s WHERE k > 1) ORDER BY a;
WITH s(k) AS (SELECT a FROM t) SELECT (SELECT max(k) FROM s);
WITH s(k) AS (SELECT a FROM t)
  SELECT EXISTS(SELECT 1 FROM s WHERE k = 3), EXISTS(SELECT 1 FROM s WHERE k = 9);
WITH s(k) AS (SELECT a FROM t) SELECT x FROM (SELECT k AS x FROM s WHERE k < 3) ORDER BY x;
