CREATE TABLE q3_hsc(a int) PARTITION BY HASH (a);
CREATE TABLE q3_hsc_0 PARTITION OF q3_hsc FOR VALUES WITH (MODULUS 2, REMAINDER 0);
CREATE TABLE q3_hsc_1 PARTITION OF q3_hsc FOR VALUES WITH (MODULUS 2, REMAINDER 1);
SELECT partstrat, partnatts FROM pg_partitioned_table WHERE partrelid = 'q3_hsc'::regclass;
SELECT pg_get_partkeydef('q3_hsc'::regclass);
SELECT relname, pg_get_expr(relpartbound, oid) FROM pg_class
  WHERE relname IN ('q3_hsc_0','q3_hsc_1') ORDER BY relname;
DROP TABLE q3_hsc;
