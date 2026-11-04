CREATE TABLE r10se1(a int, b int GENERATED ALWAYS AS (a * 2) VIRTUAL);
INSERT INTO r10se1(a) VALUES (3);
SELECT a, b FROM r10se1 ORDER BY a;
ALTER TABLE r10se1 ALTER COLUMN b SET EXPRESSION AS (a * 10);
SELECT a, b FROM r10se1 ORDER BY a;
SELECT attname, attgenerated FROM pg_attribute WHERE attrelid = 'r10se1'::regclass AND attnum > 0 ORDER BY attnum;
DROP TABLE r10se1;
