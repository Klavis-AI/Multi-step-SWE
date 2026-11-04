CREATE TABLE s2(i int);
COPY s2 FROM stdin;
1
2
\.
SELECT count(*) FROM s2;
DROP TABLE s2;
