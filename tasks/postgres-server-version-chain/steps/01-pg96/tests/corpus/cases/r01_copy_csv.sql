CREATE TABLE cc(i int, t text);
COPY cc FROM stdin WITH (FORMAT csv);
1,"a,b"
2,"q""q"
3,
\.
SELECT i, t, t IS NULL FROM cc ORDER BY i;
COPY cc TO stdout WITH (FORMAT csv, HEADER);
COPY cc TO stdout WITH (FORMAT csv, NULL 'NUL');
DROP TABLE cc;
