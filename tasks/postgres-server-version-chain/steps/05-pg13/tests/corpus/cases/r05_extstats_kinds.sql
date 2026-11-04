CREATE TABLE x05_37a(a int, b int, c int);
INSERT INTO x05_37a SELECT g%10, g%10, g%3 FROM generate_series(1,200) g;
CREATE STATISTICS x05_37s (ndistinct, dependencies, mcv) ON a, b FROM x05_37a;
SELECT stxname, stxkind FROM pg_statistic_ext WHERE stxname='x05_37s';
ANALYZE x05_37a;
SELECT statistics_name, attnames, kinds, n_distinct
  FROM pg_stats_ext WHERE statistics_name='x05_37s';
DROP STATISTICS x05_37s;
DROP TABLE x05_37a;
