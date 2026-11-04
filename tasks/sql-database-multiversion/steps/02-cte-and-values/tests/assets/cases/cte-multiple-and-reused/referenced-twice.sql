-- A common table expression acts like a view, so it may be referenced more
-- than once in the statement that defines it.
CREATE TABLE t(g TEXT, v INTEGER);
INSERT INTO t VALUES('a',1),('a',5),('b',2),('b',9);
WITH s(g, v) AS (SELECT g, v FROM t)
  SELECT p.g, p.v, q.v FROM s AS p JOIN s AS q ON p.g = q.g AND p.v < q.v
  ORDER BY p.g, p.v;
WITH s(g, v) AS (SELECT g, v FROM t)
  SELECT g, v FROM s AS o WHERE v = (SELECT max(v) FROM s AS i WHERE i.g = o.g)
  ORDER BY g;
WITH s(v) AS (SELECT v FROM t)
  SELECT (SELECT count(*) FROM s), (SELECT sum(v) FROM s), (SELECT max(v) FROM s);
