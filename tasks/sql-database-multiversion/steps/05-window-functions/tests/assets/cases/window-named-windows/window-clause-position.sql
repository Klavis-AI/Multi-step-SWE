-- windowfunctions.html, Introduction: "The WINDOW clause, when one is present,
-- comes after any HAVING clause and before any ORDER BY."  (PROFILE.md 5.6)
CREATE TABLE s(g TEXT, v INTEGER);
INSERT INTO s VALUES('x',1),('x',2),('y',10),('y',20),('z',100);
-- the full sequence: FROM, GROUP BY, HAVING, WINDOW, ORDER BY
SELECT g, sum(v) AS total, sum(sum(v)) OVER w AS running
FROM s
GROUP BY g
HAVING sum(v) > 2
WINDOW w AS (ORDER BY g)
ORDER BY g;
-- with no GROUP BY or HAVING, the clause sits between FROM and ORDER BY
SELECT g, v, count(*) OVER w AS n
FROM s
WINDOW w AS (PARTITION BY g)
ORDER BY g, v;
-- and with a WHERE clause before it
SELECT g, v, sum(v) OVER w AS running
FROM s
WHERE v >= 2
WINDOW w AS (ORDER BY v)
ORDER BY v;
