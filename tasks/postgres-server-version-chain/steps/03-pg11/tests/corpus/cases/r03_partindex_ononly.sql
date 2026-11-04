CREATE TABLE q3_pio(a int, b text) PARTITION BY RANGE (a);
CREATE TABLE q3_pio_1 PARTITION OF q3_pio FOR VALUES FROM (1) TO (10);
CREATE INDEX q3_pio_b ON ONLY q3_pio (b);
SELECT c.relname, c.relkind, i.indisvalid FROM pg_index i JOIN pg_class c ON c.oid = i.indexrelid
  WHERE c.relname = 'q3_pio_b';
CREATE INDEX q3_pio_1_b ON q3_pio_1 (b);
ALTER INDEX q3_pio_b ATTACH PARTITION q3_pio_1_b;
SELECT c.relname, i.indisvalid FROM pg_index i JOIN pg_class c ON c.oid = i.indexrelid
  WHERE c.relname = 'q3_pio_b';
DROP TABLE q3_pio;
