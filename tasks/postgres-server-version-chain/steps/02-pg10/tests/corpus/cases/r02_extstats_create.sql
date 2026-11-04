CREATE TABLE q2_esa(a int, b int, c text);
INSERT INTO q2_esa SELECT g % 10, g % 10, 't' FROM generate_series(1,100) g;
CREATE STATISTICS q2_esa_s ON a, b FROM q2_esa;
ANALYZE q2_esa;
SELECT stxname, stxkeys::text FROM pg_statistic_ext WHERE stxname = 'q2_esa_s';
DROP STATISTICS q2_esa_s;
SELECT count(*) FROM pg_statistic_ext WHERE stxname = 'q2_esa_s';
DROP TABLE q2_esa;
