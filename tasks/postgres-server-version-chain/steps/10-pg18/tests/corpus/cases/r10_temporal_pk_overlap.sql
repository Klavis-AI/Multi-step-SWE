CREATE TABLE r10tp2(id int4range, valid_at daterange, PRIMARY KEY (id, valid_at WITHOUT OVERLAPS));
INSERT INTO r10tp2 VALUES (int4range(1,2), daterange('2020-01-01','2020-06-01'));
INSERT INTO r10tp2 VALUES (int4range(1,2), daterange('2020-03-01','2020-09-01'));
INSERT INTO r10tp2 VALUES (int4range(1,2), daterange('2020-01-01','2020-06-01'));
UPDATE r10tp2 SET valid_at = daterange('2020-05-01','2020-07-01');
SELECT id, valid_at FROM r10tp2 ORDER BY id, valid_at;
DROP TABLE r10tp2;
