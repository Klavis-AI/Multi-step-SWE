CREATE TABLE q4_smc(a int, b int);
INSERT INTO q4_smc SELECT g % 5, g % 5 FROM generate_series(1,100) g;
CREATE STATISTICS q4_smc_s (mcv) ON a, b FROM q4_smc;
ANALYZE q4_smc;
SELECT stxname, stxkind::text FROM pg_statistic_ext WHERE stxname = 'q4_smc_s';
CREATE STATISTICS q4_smc_t (mcv, ndistinct) ON a, b FROM q4_smc;
SELECT stxname, stxkind::text FROM pg_statistic_ext WHERE stxname = 'q4_smc_t';
DROP TABLE q4_smc;
