-- windowfunctions.html: "An aggregate window function is similar to an
-- aggregate function, except adding it to a query does not change the number
-- of rows returned."  Over the whole partition the two forms must agree, which
-- is the strongest statement the page makes about what an aggregate window
-- function returns.  (PROFILE.md 5.5)
CREATE TABLE t(k INTEGER PRIMARY KEY, g TEXT, v INTEGER);
INSERT INTO t VALUES(1,'x',10),(2,'x',20),(3,'y',30),(4,'y',40),(5,'y',50);
SELECT count(*) FROM t;
SELECT count(*) FROM (SELECT k, sum(v) OVER (PARTITION BY g) FROM t);
-- partitioned window aggregates, then the same aggregates under GROUP BY
SELECT DISTINCT g, sum(v) OVER (PARTITION BY g), count(*) OVER (PARTITION BY g),
                max(v) OVER (PARTITION BY g), min(v) OVER (PARTITION BY g)
  FROM t ORDER BY g;
SELECT g, sum(v), count(*), max(v), min(v) FROM t GROUP BY g ORDER BY g;
-- and over the whole table, with no PARTITION BY and no ORDER BY
SELECT DISTINCT min(v) OVER (), max(v) OVER (), avg(v) OVER (), total(v) OVER (),
                sum(v) OVER (), count(*) OVER ()
  FROM t;
SELECT min(v), max(v), avg(v), total(v), sum(v), count(*) FROM t;
