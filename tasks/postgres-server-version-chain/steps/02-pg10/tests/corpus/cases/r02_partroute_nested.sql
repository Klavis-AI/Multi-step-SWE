CREATE TABLE q2_rtn(a int, b int) PARTITION BY RANGE (a);
CREATE TABLE q2_rtn_1 PARTITION OF q2_rtn FOR VALUES FROM (1) TO (10) PARTITION BY RANGE (b);
CREATE TABLE q2_rtn_1a PARTITION OF q2_rtn_1 FOR VALUES FROM (1) TO (5);
CREATE TABLE q2_rtn_1b PARTITION OF q2_rtn_1 FOR VALUES FROM (5) TO (9);
INSERT INTO q2_rtn VALUES (2,2),(3,6);
SELECT tableoid::regclass::text AS part, a, b FROM q2_rtn ORDER BY a;
INSERT INTO q2_rtn VALUES (2,99);
SELECT c.relname, c.relkind FROM pg_inherits i JOIN pg_class c ON c.oid = i.inhrelid
  WHERE i.inhparent IN ('q2_rtn'::regclass,'q2_rtn_1'::regclass) ORDER BY 1;
DROP TABLE q2_rtn;
