CREATE TABLE q3_pmb(a int, b text) PARTITION BY RANGE (a);
CREATE TABLE q3_pmb_1 PARTITION OF q3_pmb FOR VALUES FROM (1) TO (10);
CREATE TABLE q3_pmb_2 PARTITION OF q3_pmb FOR VALUES FROM (10) TO (20);
INSERT INTO q3_pmb VALUES (5,'x'),(6,'y');
UPDATE q3_pmb SET a = a + 10 WHERE a = 5;
SELECT tableoid::regclass::text AS part, a, b FROM q3_pmb ORDER BY a;
SELECT count(*) FROM q3_pmb_1;
SELECT count(*) FROM q3_pmb_2;
DROP TABLE q3_pmb;
