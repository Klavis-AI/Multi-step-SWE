CREATE TABLE x06_19a(a int, b int);
INSERT INTO x06_19a VALUES (1,2),(2,3),(3,1);
WITH RECURSIVE w(a,b) AS (
  SELECT a,b FROM x06_19a WHERE a = 1
  UNION ALL
  SELECT g.a,g.b FROM x06_19a g JOIN w ON g.a = w.b
) CYCLE a SET is_cycle TO 'Y' DEFAULT 'N' USING path
SELECT a, b, is_cycle FROM w ORDER BY a, b, is_cycle;
WITH RECURSIVE w(a,b) AS (
  SELECT a,b FROM x06_19a WHERE a = 1
  UNION ALL
  SELECT g.a,g.b FROM x06_19a g JOIN w ON g.a = w.b
) CYCLE a SET is_cycle TO true DEFAULT false USING path
SELECT count(*), count(*) FILTER (WHERE is_cycle) FROM w;
DROP TABLE x06_19a;
