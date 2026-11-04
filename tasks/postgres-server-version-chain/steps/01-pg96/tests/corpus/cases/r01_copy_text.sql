CREATE TABLE cp(i int, t text, n numeric);
COPY cp FROM stdin;
1	alpha	1.5
2	beta with \t tab	2.5
3	\N	\N
\.
SELECT * FROM cp ORDER BY i;
COPY cp TO stdout;
COPY cp (i,t) TO stdout;
COPY (SELECT i*10 FROM cp ORDER BY i) TO stdout;
DROP TABLE cp;
