-- PROFILE.md 5.1: the SELECT a window function appears in "may be any of the
-- SELECTs step 1 section 1 and step 2 sections 2.1-2.2 put in scope: a
-- top-level query, the body of a common table expression, a FROM subquery, one
-- arm of a compound select".
CREATE TABLE t(k INTEGER PRIMARY KEY, g TEXT, v INTEGER);
INSERT INTO t VALUES(1,'a',30),(2,'a',10),(3,'b',20),(4,'b',40);
-- a window function in the body of a common table expression
WITH r AS (SELECT k, g, rank() OVER (ORDER BY v) AS rk FROM t)
SELECT k, g, rk FROM r ORDER BY k;
-- a window function over the rows a common table expression produces
WITH c(k,v) AS (SELECT k, v FROM t WHERE v >= 20)
SELECT k, row_number() OVER (ORDER BY v) AS rn FROM c ORDER BY k;
-- and over a VALUES query given column names by the WITH clause (step 2)
WITH v(x) AS (VALUES(3),(1),(2))
SELECT x, row_number() OVER (ORDER BY x) AS rn, sum(x) OVER (ORDER BY x) AS s
  FROM v ORDER BY x;
-- a window function in a FROM subquery
SELECT k, rn FROM (SELECT k, row_number() OVER (ORDER BY v) AS rn FROM t)
  ORDER BY k;
