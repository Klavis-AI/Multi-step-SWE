CREATE TABLE x06_80a(a int, b int);
CREATE STATISTICS x06_80s ON (a+b) FROM x06_80a;
SELECT stxname, stxkind FROM pg_statistic_ext WHERE stxname='x06_80s';
SELECT pg_get_statisticsobjdef(oid) FROM pg_statistic_ext WHERE stxname='x06_80s';
CREATE STATISTICS x06_80s2 ON a FROM x06_80a;
DROP STATISTICS x06_80s;
DROP TABLE x06_80a;
