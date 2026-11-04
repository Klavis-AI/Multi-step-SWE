CREATE TABLE t(g TEXT, v INTEGER);
INSERT INTO t VALUES('a',1),('a',5),('b',2),('b',9),('c',4);
WITH s(g, v) AS (SELECT g, v FROM t)
  SELECT g, count(v), sum(v) FROM s GROUP BY g HAVING sum(v) > 4 ORDER BY g;
WITH s(g, v) AS (SELECT g, v FROM t) SELECT g, v FROM s ORDER BY v DESC LIMIT 2;
WITH s(g, v) AS (SELECT g, v FROM t) SELECT DISTINCT g FROM s ORDER BY g;
WITH s(v) AS (SELECT v FROM t) SELECT v FROM s ORDER BY v LIMIT 2 OFFSET 1;
