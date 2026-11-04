CREATE TABLE q4_cwb(a int, b text);
COPY q4_cwb FROM stdin WHERE a > 1;
1	one
2	two
3	three
\.
SELECT a, b FROM q4_cwb ORDER BY a;
DROP TABLE q4_cwb;
