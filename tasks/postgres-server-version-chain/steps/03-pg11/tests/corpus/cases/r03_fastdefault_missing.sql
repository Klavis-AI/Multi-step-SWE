CREATE TABLE q3_fdm(a int);
INSERT INTO q3_fdm SELECT generate_series(1,3);
ALTER TABLE q3_fdm ADD COLUMN b int NOT NULL DEFAULT 7;
ALTER TABLE q3_fdm ADD COLUMN c text DEFAULT 'zz';
SELECT attname, atthasmissing, attmissingval::text, attnotnull FROM pg_attribute
  WHERE attrelid = 'q3_fdm'::regclass AND attnum > 0 ORDER BY attnum;
SELECT a, b, c FROM q3_fdm ORDER BY a;
DROP TABLE q3_fdm;
