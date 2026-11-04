CREATE TABLE x05_41a(a int, b int, c int);
CREATE STATISTICS x05_41s (ndistinct) ON a, b FROM x05_41a;
SELECT count(*) FROM pg_statistic_ext WHERE stxname='x05_41s';
ALTER TABLE x05_41a DROP COLUMN c;
SELECT count(*) FROM pg_statistic_ext WHERE stxname='x05_41s';
ALTER TABLE x05_41a DROP COLUMN b;
SELECT count(*) FROM pg_statistic_ext WHERE stxname='x05_41s';
DROP TABLE x05_41a;
