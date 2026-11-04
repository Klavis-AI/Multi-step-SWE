-- A VALUES clause is a query (PROFILE.md 2.2), and giving it column names
-- through a common table expression's column list is how the verifier reaches
-- its columns.  JSON documents supplied that way behave exactly as stored
-- ones do.
WITH v(x) AS (VALUES('{"a":7}'),('{"a":8}'),('{"a":9}'))
SELECT json_extract(x,'$.a') FROM v ORDER BY 1;
WITH v(x) AS (VALUES('{"a":7}'),('{"a":8}'))
SELECT count(*), sum(json_extract(x,'$.a')) FROM v;
WITH v(k,n) AS (VALUES('a',1),('b',2))
SELECT json_object(k,n) FROM v ORDER BY k;
WITH v(x) AS (VALUES('[1,2,3]'),('[]'),('{"z":1}'))
SELECT json_type(x), json_array_length(x) FROM v ORDER BY 1, 2;
-- a VALUES clause used directly as a FROM subquery, reached by ordinal
SELECT count(*) FROM (VALUES('{"a":1}'),('{"a":2}'));
-- json1 output flowing into an INSERT sourced from a VALUES-bodied CTE
CREATE TABLE t(id INTEGER PRIMARY KEY, j TEXT);
WITH v(id,k,n) AS (VALUES(1,'p',10),(2,'q',20))
INSERT INTO t(id,j) SELECT id, json_object(k,n) FROM v;
SELECT id, j, json_valid(j) FROM t ORDER BY id;
