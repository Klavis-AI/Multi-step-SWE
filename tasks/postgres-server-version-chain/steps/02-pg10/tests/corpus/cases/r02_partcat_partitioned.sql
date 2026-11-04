CREATE TABLE q2_pcp(a int, b text) PARTITION BY LIST (b);
CREATE TABLE q2_pcp_1 PARTITION OF q2_pcp FOR VALUES IN ('x');
SELECT partstrat, partnatts, partattrs::text FROM pg_partitioned_table
  WHERE partrelid = 'q2_pcp'::regclass;
SELECT count(*) FROM pg_partitioned_table WHERE partrelid = 'q2_pcp_1'::regclass;
SELECT pg_get_partkeydef('q2_pcp'::regclass), pg_get_partkeydef('q2_pcp_1'::regclass);
DROP TABLE q2_pcp;
