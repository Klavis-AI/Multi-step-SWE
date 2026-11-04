-- PROFILE.md 5.7: a window function's input is "the results set of a SELECT
-- statement" (windowfunctions.html), so in a query with GROUP BY the rows it
-- sees are the grouped rows -- one per group -- and it may take the grouping
-- expressions and the aggregates of that same SELECT as its arguments and as
-- its window ORDER BY terms.
CREATE TABLE sales(region TEXT, amt INTEGER);
INSERT INTO sales VALUES('east',10),('east',20),('west',5),('west',15),('north',100);
SELECT region, sum(amt) AS total,
       sum(sum(amt)) OVER (ORDER BY region) AS running,
       rank() OVER (ORDER BY sum(amt) DESC) AS rk,
       count(*) OVER () AS grouped_rows
FROM sales GROUP BY region ORDER BY region;
-- a PARTITION BY over one of the grouping expressions
CREATE TABLE t(g1 TEXT, g2 TEXT);
INSERT INTO t VALUES('a','p'),('a','p'),('a','q'),
                    ('b','r'),('b','r'),('b','s'),('b','s');
SELECT g1, g2, count(*) AS n, sum(count(*)) OVER (PARTITION BY g1) AS per_g1
FROM t GROUP BY g1, g2 ORDER BY g1, g2;
