CREATE TABLE x06_20a(a int, b int);
INSERT INTO x06_20a VALUES (1,2),(2,3),(3,1);
WITH RECURSIVE w(a,b) AS (
  SELECT a,b FROM x06_20a WHERE a = 1
  UNION ALL
  SELECT g.a,g.b FROM x06_20a g JOIN w ON g.a = w.b
) CYCLE a SET is_cycle USING path
SELECT a, b, is_cycle, path FROM w ORDER BY a, b, is_cycle;
DROP TABLE x06_20a;
