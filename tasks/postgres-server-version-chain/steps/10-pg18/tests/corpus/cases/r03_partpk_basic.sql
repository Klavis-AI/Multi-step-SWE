CREATE TABLE q3_ppb(a int, b text, PRIMARY KEY (a)) PARTITION BY RANGE (a);
CREATE TABLE q3_ppb_1 PARTITION OF q3_ppb FOR VALUES FROM (1) TO (10);
CREATE TABLE q3_ppb_2 PARTITION OF q3_ppb FOR VALUES FROM (10) TO (20);
INSERT INTO q3_ppb VALUES (1,'x'),(11,'y');
INSERT INTO q3_ppb VALUES (1,'dup');
INSERT INTO q3_ppb VALUES (NULL,'nokey');
SELECT conname, contype FROM pg_constraint WHERE conrelid = 'q3_ppb'::regclass ORDER BY conname;
SELECT a, b FROM q3_ppb ORDER BY a;
DROP TABLE q3_ppb;
