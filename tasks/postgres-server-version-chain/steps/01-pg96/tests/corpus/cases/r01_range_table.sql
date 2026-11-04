CREATE TABLE rg(i int primary key, span int4range, d daterange);
INSERT INTO rg VALUES (1,'[1,5)','[2020-01-01,2020-06-01)'),(2,'[5,9)','[2020-06-01,2021-01-01)');
SELECT i, span, d FROM rg ORDER BY i;
SELECT i FROM rg WHERE span @> 6 ORDER BY i;
SELECT i FROM rg ORDER BY span DESC;
DROP TABLE rg;
