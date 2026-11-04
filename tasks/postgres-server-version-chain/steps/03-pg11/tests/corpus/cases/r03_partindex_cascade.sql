CREATE TABLE q3_pic(a int, b text) PARTITION BY RANGE (a);
CREATE TABLE q3_pic_1 PARTITION OF q3_pic FOR VALUES FROM (1) TO (10);
CREATE TABLE q3_pic_2 PARTITION OF q3_pic FOR VALUES FROM (10) TO (20);
CREATE INDEX q3_pic_b ON q3_pic (b);
SELECT relname, relkind FROM pg_class WHERE relname IN ('q3_pic_b','q3_pic_1_b_idx','q3_pic_2_b_idx')
  ORDER BY relname;
INSERT INTO q3_pic VALUES (1,'x'),(11,'y');
SELECT a, b FROM q3_pic WHERE b = 'y';
DROP TABLE q3_pic;
