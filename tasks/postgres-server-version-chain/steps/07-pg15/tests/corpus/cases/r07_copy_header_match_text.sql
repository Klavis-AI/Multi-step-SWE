CREATE TABLE x07_70a(a int, b text);
COPY x07_70a FROM stdin WITH (HEADER MATCH);
a	b
1	x
\.
SELECT a, b FROM x07_70a ORDER BY a;
COPY x07_70a FROM stdin WITH (FORMAT csv, HEADER true);
zz,yy
2,q
\.
SELECT a, b FROM x07_70a ORDER BY a;
DROP TABLE x07_70a;
