CREATE TABLE q2_esb(a int, b int);
CREATE STATISTICS q2_esb_nd (ndistinct) ON a, b FROM q2_esb;
CREATE STATISTICS q2_esb_dep (dependencies) ON a, b FROM q2_esb;
SELECT stxname, stxkind::text FROM pg_statistic_ext
  WHERE stxname IN ('q2_esb_nd','q2_esb_dep') ORDER BY stxname;
SELECT attname FROM pg_attribute WHERE attrelid = 'pg_statistic_ext'::regclass
  AND attnum > 0 ORDER BY attnum;
DROP TABLE q2_esb;
SELECT count(*) FROM pg_statistic_ext WHERE stxname IN ('q2_esb_nd','q2_esb_dep');
