CREATE TABLE q2_pro(a int) PARTITION BY RANGE (a);
CREATE TABLE q2_pro_1 PARTITION OF q2_pro FOR VALUES FROM (1) TO (10);
CREATE TABLE q2_pro_2 PARTITION OF q2_pro FOR VALUES FROM (5) TO (15);
CREATE TABLE q2_pro_3 PARTITION OF q2_pro FOR VALUES FROM (10) TO (5);
CREATE TABLE q2_pro_4 PARTITION OF q2_pro FOR VALUES IN (1);
SELECT c.relname FROM pg_inherits i JOIN pg_class c ON c.oid = i.inhrelid
  WHERE i.inhparent = 'q2_pro'::regclass ORDER BY 1;
DROP TABLE q2_pro;
