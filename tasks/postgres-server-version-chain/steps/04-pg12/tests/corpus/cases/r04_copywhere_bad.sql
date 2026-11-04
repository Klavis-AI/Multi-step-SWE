CREATE TABLE q4_cwx(a int, b text);
COPY q4_cwx TO stdout WHERE a > 1;
COPY q4_cwx TO stdout WITH (FORMAT csv) WHERE a > 1;
COPY q4_cwx FROM stdin WHERE nosuchcol > 1;
1	x
\.
COPY q4_cwx FROM stdin WHERE count(*) > 1;
2	y
\.
COPY q4_cwx FROM stdin WHERE (SELECT true);
3	z
\.
SELECT count(*) FROM q4_cwx;
DROP TABLE q4_cwx;
