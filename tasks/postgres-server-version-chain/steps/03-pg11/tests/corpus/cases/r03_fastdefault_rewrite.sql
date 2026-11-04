CREATE TABLE q3_fdr(a int);
INSERT INTO q3_fdr SELECT generate_series(1,3);
ALTER TABLE q3_fdr ADD COLUMN b int NOT NULL DEFAULT 7;
SELECT attname, atthasmissing FROM pg_attribute
  WHERE attrelid = 'q3_fdr'::regclass AND attnum > 0 ORDER BY attnum;
ALTER TABLE q3_fdr ALTER COLUMN b TYPE bigint;
SELECT attname, atthasmissing, atttypid::regtype::text FROM pg_attribute
  WHERE attrelid = 'q3_fdr'::regclass AND attnum > 0 ORDER BY attnum;
SELECT a, b FROM q3_fdr ORDER BY a;
DROP TABLE q3_fdr;
