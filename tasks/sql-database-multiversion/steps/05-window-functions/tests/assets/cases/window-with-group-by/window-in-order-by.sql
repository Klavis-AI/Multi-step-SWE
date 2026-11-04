-- PROFILE.md 5.7: "a window function may appear in the statement's ORDER BY,
-- which is evaluated after the window functions have been computed" --
-- windowfunctions.html puts the ORDER BY of a SELECT among the two places a
-- window function may appear.
CREATE TABLE t(k INTEGER PRIMARY KEY, v INTEGER);
INSERT INTO t VALUES(1,30),(2,10),(3,20);
-- row_number() over a total window ORDER BY is itself a total sort key
SELECT k, v FROM t ORDER BY row_number() OVER (ORDER BY v);
SELECT k, v FROM t ORDER BY rank() OVER (ORDER BY v DESC), k;
-- an ORDER BY term naming the alias of a window function result column
SELECT k, row_number() OVER (ORDER BY v) AS rn FROM t ORDER BY rn;
-- and in a grouped query
CREATE TABLE s(g TEXT, v INTEGER);
INSERT INTO s VALUES('a',1),('b',50),('c',10);
SELECT g, sum(v) AS total FROM s GROUP BY g
ORDER BY rank() OVER (ORDER BY sum(v) DESC);
