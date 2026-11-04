CREATE TABLE q4_cwe(a int, b text);
COPY q4_cwe FROM stdin WITH (FORMAT csv) WHERE b LIKE 'o%';
1,one
2,two
3,other
\.
SELECT a, b FROM q4_cwe ORDER BY a;
COPY q4_cwe FROM stdin WHERE a IS NULL;
4	four
\.
SELECT count(*) FROM q4_cwe;
DROP TABLE q4_cwe;
