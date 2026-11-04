CREATE TABLE r10a1(id int, v int[]);
INSERT INTO r10a1 VALUES (1,'{3,1,2}'),(2,'{}'),(3,NULL);
SELECT id, array_sort(v), array_reverse(v) FROM r10a1 ORDER BY id;
SELECT id, array_sort(v, false) FROM r10a1 WHERE v IS NOT NULL ORDER BY id;
SELECT array_sort(array_agg(id)) FROM r10a1;
DROP TABLE r10a1;
