CREATE TABLE x07_69a(a int, b text);
COPY x07_69a FROM stdin WITH (FORMAT csv, HEADER MATCH);
b,a
1,x
\.
SELECT count(*) FROM x07_69a;
COPY x07_69a FROM stdin WITH (FORMAT csv, HEADER MATCH);
a
1
\.
SELECT count(*) FROM x07_69a;
DROP TABLE x07_69a;
