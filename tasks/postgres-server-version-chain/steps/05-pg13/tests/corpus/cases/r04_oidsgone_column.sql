CREATE TABLE q4_ogo(a int) WITH OIDS;
INSERT INTO q4_ogo VALUES (1),(2);
SELECT count(*) FROM q4_ogo WHERE oid > 0;
SELECT a FROM q4_ogo ORDER BY a;
SELECT attname FROM pg_attribute WHERE attrelid = 'q4_ogo'::regclass AND attnum < 0 ORDER BY attnum;
DROP TABLE IF EXISTS q4_ogo;
