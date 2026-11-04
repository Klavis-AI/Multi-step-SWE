CREATE TABLE ce(i int);
COPY ce FROM stdin;
notanint
\.
SELECT count(*) FROM ce;
COPY nosuchtable_r01 FROM stdin;
\.
DROP TABLE ce;
