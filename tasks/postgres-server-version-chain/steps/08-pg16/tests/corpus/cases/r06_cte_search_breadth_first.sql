CREATE TABLE x06_16a(id int, parent int, label text);
INSERT INTO x06_16a VALUES (1,NULL,'r'),(2,1,'a'),(3,1,'b'),(4,2,'c'),(5,3,'d');
WITH RECURSIVE t(id,parent,label) AS (
  SELECT id,parent,label FROM x06_16a WHERE parent IS NULL
  UNION ALL
  SELECT g.id,g.parent,g.label FROM x06_16a g JOIN t ON g.parent = t.id
) SEARCH BREADTH FIRST BY id SET ord
SELECT id, label, ord FROM t ORDER BY ord;
DROP TABLE x06_16a;
