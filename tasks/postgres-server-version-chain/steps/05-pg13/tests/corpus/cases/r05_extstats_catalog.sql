CREATE TABLE x05_38a(a int, b int);
CREATE STATISTICS x05_38s (ndistinct) ON a, b FROM x05_38a;
SELECT s.stxname, cl.relname, s.stxkind FROM pg_statistic_ext s
  JOIN pg_class cl ON cl.oid = s.stxrelid WHERE s.stxname='x05_38s';
SELECT pg_get_statisticsobjdef(oid) FROM pg_statistic_ext WHERE stxname='x05_38s';
SELECT count(*) FROM pg_statistic_ext WHERE stxname='x05_38s';
DROP STATISTICS x05_38s;
SELECT count(*) FROM pg_statistic_ext WHERE stxname='x05_38s';
DROP TABLE x05_38a;
