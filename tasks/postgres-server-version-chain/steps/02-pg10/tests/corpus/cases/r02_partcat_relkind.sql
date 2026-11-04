CREATE TABLE q2_pck(a int) PARTITION BY RANGE (a);
CREATE TABLE q2_pck_1 PARTITION OF q2_pck FOR VALUES FROM (1) TO (10);
CREATE TABLE q2_pck_plain(a int);
SELECT relname, relkind, relispartition, relhassubclass FROM pg_class
  WHERE relname IN ('q2_pck','q2_pck_1','q2_pck_plain') ORDER BY relname;
SELECT relname, relhasindex, relpersistence FROM pg_class
  WHERE relname IN ('q2_pck','q2_pck_1') ORDER BY relname;
DROP TABLE IF EXISTS q2_pck;
DROP TABLE IF EXISTS q2_pck_plain;
