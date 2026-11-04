CREATE TABLE q2_rtc(a int, b text) PARTITION BY RANGE (a);
CREATE TABLE q2_rtc_1 PARTITION OF q2_rtc FOR VALUES FROM (1) TO (5);
CREATE TABLE q2_rtc_2 PARTITION OF q2_rtc FOR VALUES FROM (5) TO (9);
COPY q2_rtc FROM stdin;
1	one
5	five
7	seven
\.
SELECT tableoid::regclass::text AS part, a, b FROM q2_rtc ORDER BY a;
COPY q2_rtc FROM stdin;
99	oops
\.
SELECT count(*) FROM q2_rtc;
COPY (SELECT a FROM q2_rtc ORDER BY a) TO stdout;
DROP TABLE q2_rtc;
