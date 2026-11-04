-- PROFILE.md 5.7: the rows a window function sees are the rows the grouping
-- produced, "after HAVING has discarded any" -- which follows from the WINDOW
-- clause coming "after any HAVING clause" (windowfunctions.html).
CREATE TABLE t(g TEXT, v INTEGER);
INSERT INTO t VALUES('a',1),('a',2),('b',10),('c',100),('c',200);
-- three groups, and the window function sees all three
SELECT g, sum(v) AS s, count(*) OVER () AS rows_seen,
       sum(sum(v)) OVER () AS grand
FROM t GROUP BY g ORDER BY g;
-- HAVING discards group 'a' first, so neither count(*) nor the running total
-- includes it
SELECT g, sum(v) AS s, count(*) OVER () AS rows_seen,
       sum(sum(v)) OVER () AS grand
FROM t GROUP BY g HAVING sum(v) > 5 ORDER BY g;
-- and a HAVING that discards everything leaves no rows at all
SELECT g, count(*) OVER () FROM t GROUP BY g HAVING sum(v) > 1000 ORDER BY g;
SELECT 'done';
