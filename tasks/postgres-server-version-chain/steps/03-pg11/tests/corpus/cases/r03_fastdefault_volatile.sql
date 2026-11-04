CREATE SEQUENCE q3_fdv_s;
CREATE TABLE q3_fdv(a int);
INSERT INTO q3_fdv SELECT generate_series(1,3);
ALTER TABLE q3_fdv ADD COLUMN b int NOT NULL DEFAULT nextval('q3_fdv_s');
SELECT a, b FROM q3_fdv ORDER BY a;
SELECT attname, atthasmissing, attmissingval IS NULL AS no_missing FROM pg_attribute
  WHERE attrelid = 'q3_fdv'::regclass AND attnum > 0 ORDER BY attnum;
INSERT INTO q3_fdv(a) VALUES (4);
SELECT a, b FROM q3_fdv ORDER BY a;
DROP TABLE q3_fdv;
DROP SEQUENCE q3_fdv_s;
