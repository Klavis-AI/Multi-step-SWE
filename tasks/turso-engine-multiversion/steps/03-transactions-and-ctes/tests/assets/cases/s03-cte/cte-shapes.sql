-- R03.6: more shapes of the same non-recursive WITH ... AS (...) SELECT --
-- constants with no table under them, expression columns, grouping done
-- inside the CTE, and an inner ORDER BY/LIMIT under a differently ordered
-- outer query.
-- More shapes of the same construct: a CTE built from constants with no table
-- under it at all, a CTE whose columns are expressions rather than columns, a
-- CTE that is grouped before the outer query sees it, a CTE that orders and
-- limits inside itself while the outer query orders differently, and a CTE
-- joined back to the table it came from.
CREATE TABLE t(a INTEGER PRIMARY KEY, b TEXT, n INTEGER);
INSERT INTO t VALUES(1,'x',10);
INSERT INTO t VALUES(2,'y',20);
INSERT INTO t VALUES(3,'x',30);
INSERT INTO t VALUES(4,'z',40);
WITH c AS (SELECT 1 AS x, 'one' AS y)
  SELECT 'constants', x, y FROM c;
WITH c AS (SELECT a, n FROM t WHERE a > 1)
  SELECT 'count over the cte', count(*), sum(n), min(a), max(a) FROM c;
WITH c AS (SELECT a, n * 2 AS d FROM t)
  SELECT 'expression column', a, d FROM c WHERE d > 40 ORDER BY a;
WITH c AS (SELECT b, count(*) AS k, sum(n) AS s FROM t GROUP BY b)
  SELECT 'grouped inside', b, k, s FROM c ORDER BY b DESC;
WITH c AS (SELECT a, n FROM t ORDER BY n DESC LIMIT 3)
  SELECT 'ordered inside', a, n FROM c ORDER BY a;
WITH c AS (SELECT a, n FROM t)
  SELECT 'joined back', c.a, t.b, c.n FROM c JOIN t ON t.a = c.a WHERE c.n >= 20 ORDER BY c.a;
