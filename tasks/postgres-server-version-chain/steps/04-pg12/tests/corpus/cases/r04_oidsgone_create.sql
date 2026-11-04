CREATE TABLE q4_ogc(a int) WITH OIDS;
CREATE TABLE q4_ogd(a int) WITHOUT OIDS;
INSERT INTO q4_ogd VALUES (1);
SELECT a FROM q4_ogd ORDER BY a;
SELECT count(*) FROM pg_class WHERE relname = 'q4_ogc';
DROP TABLE q4_ogd;
DROP TABLE IF EXISTS q4_ogc;
