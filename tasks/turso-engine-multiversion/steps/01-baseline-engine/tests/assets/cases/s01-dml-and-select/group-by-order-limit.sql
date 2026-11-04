-- R01.2: the GROUP BY, ORDER BY and LIMIT half of the same requirement.
--
-- R01.2 names WHERE, ORDER BY, GROUP BY and LIMIT; `insert-select-where.sql`
-- covers the first two and this case covers the other two. The second query
-- orders by an aggregate rather than by a column and then truncates, so a
-- build that applies LIMIT before ordering, or that groups after limiting,
-- returns a different pair of rows. `k` is the tie-breaker in the ORDER BY so
-- that the expected output is a total order and not a choice between two
-- equally correct renderings. Replayed against stock sqlite3 automatically.
CREATE TABLE g(k TEXT, v INTEGER);
INSERT INTO g VALUES('a',1),('b',2),('a',3),('b',4),('c',5);
SELECT k, sum(v) FROM g GROUP BY k ORDER BY k;
SELECT k, count(*) FROM g GROUP BY k ORDER BY count(*) DESC, k LIMIT 2;
