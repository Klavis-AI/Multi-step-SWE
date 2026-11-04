CREATE TABLE q4_isc(a int, b text);
COPY q4_isc FROM stdin;
notanint	x
\.
SELECT count(*) FROM q4_isc;
COPY q4_isc FROM stdin WITH (FORMAT csv);
1,ok
\.
SELECT a, b FROM q4_isc ORDER BY a;
DROP TABLE q4_isc;
