-- A correlated subquery over a common table expression.
CREATE TABLE t(g TEXT, v INTEGER);
INSERT INTO t VALUES('a',1),('a',5),('b',2),('b',9);
WITH s(g, v) AS (SELECT g, v FROM t)
  SELECT o.g, (SELECT count(*) FROM s AS i WHERE i.g = o.g) FROM t AS o WHERE o.v = 1;
WITH s(g, v) AS (SELECT g, v FROM t)
  SELECT o.g, o.v FROM t AS o WHERE o.v = (SELECT max(i.v) FROM s AS i WHERE i.g = o.g)
  ORDER BY o.g;
WITH s(g, v) AS (SELECT g, v FROM t)
  SELECT g, v FROM s AS o WHERE NOT EXISTS(SELECT 1 FROM s AS i WHERE i.g = o.g AND i.v > o.v)
  ORDER BY g;
