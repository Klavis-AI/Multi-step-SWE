CREATE TABLE af(g int, v int);
INSERT INTO af VALUES (1,1),(1,1),(1,2),(2,3);
SELECT g, count(*) FILTER (WHERE v>1), sum(DISTINCT v) FROM af GROUP BY g ORDER BY g;
SELECT count(DISTINCT v), count(v) FROM af;
SELECT sum(v) FROM af GROUP BY g HAVING sum(v) > 3;
DROP TABLE af;
