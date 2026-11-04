CREATE TABLE r08av1(g int, v text, n int);
INSERT INTO r08av1 VALUES (1,'a',1),(1,'a',2),(2,'b',3),(2,'b',4);
SELECT g, any_value(v) FROM r08av1 GROUP BY g ORDER BY g;
SELECT g, any_value(v), count(*), sum(n) FROM r08av1 GROUP BY g ORDER BY g;
SELECT any_value(g) FROM r08av1 GROUP BY g ORDER BY 1;
DROP TABLE r08av1;
