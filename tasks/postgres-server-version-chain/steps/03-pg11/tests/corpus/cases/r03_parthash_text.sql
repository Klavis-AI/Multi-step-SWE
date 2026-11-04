CREATE TABLE q3_hst(k text, v int) PARTITION BY HASH (k);
CREATE TABLE q3_hst_0 PARTITION OF q3_hst FOR VALUES WITH (MODULUS 2, REMAINDER 0);
CREATE TABLE q3_hst_1 PARTITION OF q3_hst FOR VALUES WITH (MODULUS 2, REMAINDER 1);
INSERT INTO q3_hst VALUES ('alpha',1),('beta',2),('gamma',3),('delta',4);
SELECT k, v FROM q3_hst ORDER BY k;
SELECT count(DISTINCT tableoid) FROM q3_hst;
UPDATE q3_hst SET v = v * 10 WHERE k = 'beta';
SELECT k, v FROM q3_hst WHERE k = 'beta';
DROP TABLE q3_hst;
