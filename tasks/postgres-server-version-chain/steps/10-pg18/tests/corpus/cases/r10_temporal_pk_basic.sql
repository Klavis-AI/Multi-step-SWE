CREATE TABLE r10tp1(id int4range, valid_at daterange, PRIMARY KEY (id, valid_at WITHOUT OVERLAPS));
INSERT INTO r10tp1 VALUES (int4range(1,2), daterange('2020-01-01','2020-06-01'));
INSERT INTO r10tp1 VALUES (int4range(1,2), daterange('2020-06-01','2021-01-01'));
INSERT INTO r10tp1 VALUES (int4range(2,3), daterange('2020-01-01','2021-01-01'));
SELECT id, valid_at FROM r10tp1 ORDER BY id, valid_at;
DROP TABLE r10tp1;
