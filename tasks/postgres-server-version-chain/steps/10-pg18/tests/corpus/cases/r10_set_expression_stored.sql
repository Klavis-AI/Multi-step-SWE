CREATE TABLE r10se2(a int, b int GENERATED ALWAYS AS (a * 2) STORED);
INSERT INTO r10se2(a) VALUES (4);
ALTER TABLE r10se2 ALTER COLUMN b SET EXPRESSION AS (a * 3);
SELECT a, b FROM r10se2 ORDER BY a;
SELECT attname, attgenerated FROM pg_attribute WHERE attrelid = 'r10se2'::regclass AND attnum > 0 ORDER BY attnum;
DROP TABLE r10se2;
