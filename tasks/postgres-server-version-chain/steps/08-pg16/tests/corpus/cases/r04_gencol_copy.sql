CREATE TABLE q4_gcp(a int, b int GENERATED ALWAYS AS (a * 2) STORED);
COPY q4_gcp(a) FROM stdin;
3
1
\.
SELECT a, b FROM q4_gcp ORDER BY a;
COPY q4_gcp TO stdout;
COPY q4_gcp FROM stdin;
5	10
\.
SELECT count(*) FROM q4_gcp;
DROP TABLE q4_gcp;
