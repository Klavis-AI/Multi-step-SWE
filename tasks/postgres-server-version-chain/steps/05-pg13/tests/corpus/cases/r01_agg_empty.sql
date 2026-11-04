CREATE TABLE ae(v int);
SELECT count(*), count(v), sum(v), avg(v), min(v), max(v) FROM ae;
SELECT g, count(*) FROM ae CROSS JOIN (VALUES (1)) x(g) GROUP BY g;
INSERT INTO ae VALUES (NULL);
SELECT count(*), count(v), sum(v) FROM ae;
SELECT sum(v) IS NULL FROM ae;
DROP TABLE ae;
