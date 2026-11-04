CREATE TABLE r09co2(a int, b text);
COPY r09co2 FROM stdin WITH (FORMAT csv, ON_ERROR stop);
1,ok
x,bad
\.
SELECT count(*) FROM r09co2;
DROP TABLE r09co2;
