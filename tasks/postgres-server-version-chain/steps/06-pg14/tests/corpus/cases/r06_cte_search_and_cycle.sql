CREATE TABLE x06_21a(a int, b int);
INSERT INTO x06_21a VALUES (1,2),(2,3),(3,1);
WITH RECURSIVE w(a,b) AS (
  SELECT a,b FROM x06_21a WHERE a = 1
  UNION ALL
  SELECT g.a,g.b FROM x06_21a g JOIN w ON g.a = w.b
) SEARCH DEPTH FIRST BY a SET ord CYCLE a SET is_cycle USING path
SELECT a, b, is_cycle FROM w ORDER BY ord;
DROP TABLE x06_21a;
