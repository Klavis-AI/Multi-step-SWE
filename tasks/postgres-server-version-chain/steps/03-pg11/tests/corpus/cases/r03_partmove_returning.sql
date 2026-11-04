CREATE TABLE q3_pmr(a int, b text) PARTITION BY RANGE (a);
CREATE TABLE q3_pmr_1 PARTITION OF q3_pmr FOR VALUES FROM (1) TO (10);
CREATE TABLE q3_pmr_2 PARTITION OF q3_pmr FOR VALUES FROM (10) TO (20);
INSERT INTO q3_pmr SELECT g, 'v' || g FROM generate_series(1,4) g;
WITH u AS (UPDATE q3_pmr SET a = a + 10 RETURNING a, b) SELECT a, b FROM u ORDER BY a;
SELECT tableoid::regclass::text AS part, a FROM q3_pmr ORDER BY a;
DROP TABLE q3_pmr;
