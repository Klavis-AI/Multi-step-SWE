CREATE TABLE r09co4(a int, b text);
COPY r09co4 FROM stdin WITH (FORMAT csv, ON_ERROR ignore, LOG_VERBOSITY verbose);
1,ok
x,bad
\.
SELECT a, b FROM r09co4 ORDER BY a;
DROP TABLE r09co4;
