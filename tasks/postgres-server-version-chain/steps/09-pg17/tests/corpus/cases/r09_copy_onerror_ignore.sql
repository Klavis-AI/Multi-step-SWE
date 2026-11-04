CREATE TABLE r09co1(a int, b text);
COPY r09co1 FROM stdin WITH (FORMAT csv, ON_ERROR ignore);
1,ok
x,bad
2,ok2
\.
SELECT a, b FROM r09co1 ORDER BY a;
DROP TABLE r09co1;
