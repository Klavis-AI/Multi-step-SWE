CREATE TABLE q4_gcb(a int, b int GENERATED ALWAYS AS (a * 2) STORED);
INSERT INTO q4_gcb(a) VALUES (1),(2),(10);
SELECT a, b FROM q4_gcb ORDER BY a;
UPDATE q4_gcb SET a = a + 100 WHERE a = 1;
SELECT a, b FROM q4_gcb ORDER BY a;
DELETE FROM q4_gcb WHERE b = 20;
SELECT a, b FROM q4_gcb ORDER BY a;
DROP TABLE q4_gcb;
