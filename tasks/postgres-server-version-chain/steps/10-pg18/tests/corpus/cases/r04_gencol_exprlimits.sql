CREATE TABLE q4_gcx(a int, b int GENERATED ALWAYS AS (a * 2) VIRTUAL);
CREATE TABLE q4_gcy(a int, b int GENERATED ALWAYS AS (random() * 10) STORED);
CREATE TABLE q4_gcz(a int, b int GENERATED ALWAYS AS (a * 2) STORED,
                    d int GENERATED ALWAYS AS (b * 2) STORED);
CREATE TABLE q4_gcw(a int, b int GENERATED ALWAYS AS ((SELECT 1)) STORED);
CREATE TABLE q4_gcv(a int, b int GENERATED ALWAYS AS (a * 2) STORED DEFAULT 3);
SELECT count(*) FROM pg_class WHERE relname IN ('q4_gcx','q4_gcy','q4_gcz','q4_gcw','q4_gcv');
DROP TABLE IF EXISTS q4_gcx, q4_gcy, q4_gcz, q4_gcw, q4_gcv;
