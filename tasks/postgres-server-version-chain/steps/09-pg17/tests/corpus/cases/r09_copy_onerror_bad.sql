CREATE TABLE r09co3(a int, b text);
COPY r09co3 FROM stdin WITH (FORMAT csv, ON_ERROR bogus);
\.
COPY r09co3 TO stdout WITH (FORMAT csv, ON_ERROR ignore);
SELECT count(*) FROM r09co3;
DROP TABLE r09co3;
