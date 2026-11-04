CREATE TABLE q3_pin(a int, b text) PARTITION BY RANGE (a);
CREATE TABLE q3_pin_1 PARTITION OF q3_pin FOR VALUES FROM (1) TO (10);
CREATE INDEX q3_pin_b ON q3_pin (b);
CREATE TABLE q3_pin_2 PARTITION OF q3_pin FOR VALUES FROM (10) TO (20);
SELECT count(*) FROM pg_class WHERE relname IN ('q3_pin_1_b_idx','q3_pin_2_b_idx');
CREATE TABLE q3_pin_ext(a int, b text);
ALTER TABLE q3_pin ATTACH PARTITION q3_pin_ext FOR VALUES FROM (20) TO (30);
SELECT count(*) FROM pg_index i JOIN pg_class c ON c.oid = i.indrelid
  WHERE c.relname = 'q3_pin_ext';
DROP TABLE IF EXISTS q3_pin;
DROP TABLE IF EXISTS q3_pin_ext;
