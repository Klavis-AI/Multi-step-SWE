CREATE TABLE q3_ppm(a int, b int, t text, PRIMARY KEY (a, b)) PARTITION BY RANGE (a);
CREATE TABLE q3_ppm_1 PARTITION OF q3_ppm FOR VALUES FROM (1) TO (10);
INSERT INTO q3_ppm VALUES (1,1,'x'),(1,2,'y');
INSERT INTO q3_ppm VALUES (1,1,'dup');
SELECT a, b, t FROM q3_ppm ORDER BY a, b;
SELECT conname FROM pg_constraint WHERE conrelid = 'q3_ppm_1'::regclass ORDER BY conname;
DROP TABLE q3_ppm;
