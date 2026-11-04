CREATE TABLE x06_17a(id int, parent int, label text);
INSERT INTO x06_17a VALUES (1,NULL,'r'),(2,1,'b'),(3,1,'a'),(4,2,'c');
WITH RECURSIVE t(id,parent,label) AS (
  SELECT id,parent,label FROM x06_17a WHERE parent IS NULL
  UNION ALL
  SELECT g.id,g.parent,g.label FROM x06_17a g JOIN t ON g.parent = t.id
) SEARCH DEPTH FIRST BY label, id SET ord
SELECT id, label FROM t ORDER BY ord;
WITH RECURSIVE t(id,parent,label) AS (
  SELECT id,parent,label FROM x06_17a WHERE parent IS NULL
  UNION ALL
  SELECT g.id,g.parent,g.label FROM x06_17a g JOIN t ON g.parent = t.id
) SEARCH BREADTH FIRST BY label SET ord
SELECT id, label FROM t ORDER BY ord;
DROP TABLE x06_17a;
