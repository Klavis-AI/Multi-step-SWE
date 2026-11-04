CREATE TABLE q3_cve(a int, b text);
CREATE INDEX q3_cve_i ON q3_cve (a) INCLUDE ((b || 'x'));
CREATE INDEX q3_cve_j ON q3_cve ((a + 1)) INCLUDE (b);
SELECT indexdef FROM pg_indexes WHERE indexname = 'q3_cve_j';
SELECT indnatts, indnkeyatts FROM pg_index WHERE indexrelid = 'q3_cve_j'::regclass;
INSERT INTO q3_cve VALUES (1,'p'),(2,'q');
SELECT a, b FROM q3_cve WHERE a + 1 = 3;
DROP TABLE q3_cve;
