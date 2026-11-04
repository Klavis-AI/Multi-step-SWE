-- R03.6: a non-recursive WITH ... AS (...) SELECT that filters, that
-- aggregates, that chains one CTE into the next, that joins back to the base
-- table it came from, and that orders and limits inside itself -- each
-- returning stock sqlite3's rows in stock sqlite3's order.
-- A non-recursive WITH ... AS (...) SELECT is a query, not a table. It filters,
-- it aggregates, one CTE feeds the next, and a CTE joins back against the base
-- table it was derived from. Each form returns exactly the rows stock sqlite3
-- returns for the same text, in exactly the order stock sqlite3 returns them.
CREATE TABLE t(a INTEGER PRIMARY KEY, b TEXT, n INTEGER);
INSERT INTO t VALUES(1,'x',10);
INSERT INTO t VALUES(2,'y',20);
INSERT INTO t VALUES(3,'x',30);
INSERT INTO t VALUES(4,'z',40);
WITH c AS (SELECT a, b, n FROM t WHERE n >= 20)
  SELECT 'filter', a, b, n FROM c ORDER BY a;
WITH c AS (SELECT b, sum(n) AS s, count(*) AS k FROM t GROUP BY b)
  SELECT 'aggregate', b, s, k FROM c ORDER BY b;
WITH c1 AS (SELECT a, n FROM t), c2 AS (SELECT a, n * 2 AS d FROM c1 WHERE n < 40)
  SELECT 'chained', a, d FROM c2 ORDER BY a DESC;
WITH c AS (SELECT a, n FROM t)
  SELECT 'over the cte', count(*), sum(n), min(n), max(n) FROM c;
WITH c AS (SELECT a, b FROM t WHERE b = 'x')
  SELECT 'joined back', c.a, c.b, t.n FROM c JOIN t ON t.a = c.a ORDER BY c.a;
WITH c AS (SELECT a, n FROM t ORDER BY n DESC LIMIT 2)
  SELECT 'limited', a, n FROM c ORDER BY a;
