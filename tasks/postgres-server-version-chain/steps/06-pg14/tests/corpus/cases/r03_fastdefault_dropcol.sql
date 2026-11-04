CREATE TABLE q3_fdd(a int);
INSERT INTO q3_fdd SELECT generate_series(1,3);
ALTER TABLE q3_fdd ADD COLUMN b int NOT NULL DEFAULT 11;
ALTER TABLE q3_fdd ADD COLUMN c int NOT NULL DEFAULT 22;
ALTER TABLE q3_fdd DROP COLUMN b;
SELECT a, c FROM q3_fdd ORDER BY a;
SELECT attname, atthasmissing, attmissingval::text FROM pg_attribute
  WHERE attrelid = 'q3_fdd'::regclass AND attnum > 0 AND NOT attisdropped ORDER BY attnum;
DROP TABLE q3_fdd;
