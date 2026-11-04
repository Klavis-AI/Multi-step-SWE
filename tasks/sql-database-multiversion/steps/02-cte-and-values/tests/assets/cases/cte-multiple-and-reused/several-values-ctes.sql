-- Several common table expressions in one WITH clause, each with a VALUES body.
WITH n(k, w) AS (VALUES(1,'one'),(2,'two'),(3,'three')), pick(k) AS (VALUES(1),(3))
  SELECT n.k, n.w FROM n JOIN pick ON n.k = pick.k ORDER BY n.k;
WITH a(x) AS (VALUES(10)), b(y) AS (VALUES(20)), c(z) AS (SELECT x + y FROM a, b)
  SELECT z FROM c;
WITH a(x) AS (VALUES(1),(2)), b(y) AS (VALUES(2),(3))
  SELECT (SELECT count(*) FROM a), (SELECT count(*) FROM b),
         (SELECT count(*) FROM a JOIN b ON a.x = b.y);
