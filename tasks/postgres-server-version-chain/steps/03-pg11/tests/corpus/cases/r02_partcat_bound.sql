CREATE TABLE q2_pcb(a int, b text) PARTITION BY RANGE (a);
CREATE TABLE q2_pcb_1 PARTITION OF q2_pcb FOR VALUES FROM (1) TO (10);
CREATE TABLE q2_pcb_2 PARTITION OF q2_pcb FOR VALUES FROM (MINVALUE) TO (1);
SELECT relname, pg_get_expr(relpartbound, oid) FROM pg_class
  WHERE relname IN ('q2_pcb_1','q2_pcb_2') ORDER BY relname;
SELECT relpartbound IS NULL FROM pg_class WHERE relname = 'q2_pcb';
DROP TABLE q2_pcb;
