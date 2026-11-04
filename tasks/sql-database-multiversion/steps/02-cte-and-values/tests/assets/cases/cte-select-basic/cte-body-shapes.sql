-- The body of a common table expression is a select-stmt, so it may be any
-- SELECT already in scope (PROFILE.md section 2.1).
CREATE TABLE t(g TEXT, v INTEGER);
INSERT INTO t VALUES('a',1),('a',5),('b',2),('b',9);
CREATE TABLE u(g TEXT, w TEXT);
INSERT INTO u VALUES('a','A'),('b','B');
-- a compound select
WITH s(x) AS (SELECT v FROM t WHERE g='a' UNION ALL SELECT v FROM t WHERE g='b')
  SELECT x FROM s ORDER BY x;
-- an aggregate query with GROUP BY and HAVING
WITH s(g, n) AS (SELECT g, sum(v) FROM t GROUP BY g HAVING sum(v) > 3)
  SELECT g, n FROM s ORDER BY g;
-- a join
WITH s(g, w, v) AS (SELECT t.g, u.w, t.v FROM t JOIN u ON t.g = u.g)
  SELECT g, w, v FROM s ORDER BY g, v;
-- a VALUES clause
WITH s(k, v) AS (VALUES(1,'x'),(2,'y')) SELECT k, v FROM s ORDER BY k;
