CREATE TABLE x06_18a(a int, b int);
INSERT INTO x06_18a VALUES (1,2),(2,3),(3,1);
WITH RECURSIVE w(a,b) AS (
  SELECT a,b FROM x06_18a WHERE a = 1
  UNION ALL
  SELECT g.a,g.b FROM x06_18a g JOIN w ON g.a = w.b
) CYCLE a SET is_cycle USING path
SELECT a, b, is_cycle FROM w ORDER BY a, b, is_cycle;
DROP TABLE x06_18a;
