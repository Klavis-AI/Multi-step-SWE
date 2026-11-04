CREATE TABLE q2_plt(k text, v int) PARTITION BY LIST (k);
CREATE TABLE q2_plt_a PARTITION OF q2_plt FOR VALUES IN ('a');
CREATE TABLE q2_plt_b PARTITION OF q2_plt FOR VALUES IN ('b');
INSERT INTO q2_plt VALUES ('a',1),('b',2),('a',3);
SELECT k, sum(v) FROM q2_plt GROUP BY k ORDER BY k;
SELECT tableoid::regclass::text AS part, count(*) FROM q2_plt GROUP BY 1 ORDER BY 1;
INSERT INTO q2_plt VALUES ('A',4);
DROP TABLE q2_plt;
