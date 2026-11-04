CREATE TABLE r10tp3(id int4range, valid_at daterange, PRIMARY KEY (id, valid_at WITHOUT OVERLAPS));
INSERT INTO r10tp3 VALUES (int4range(1,2), daterange('2020-01-01','2020-02-01'));
INSERT INTO r10tp3 VALUES (int4range(1,2), daterange('2020-02-01','2020-03-01'));
INSERT INTO r10tp3 VALUES (int4range(1,2), 'empty'::daterange);
INSERT INTO r10tp3 VALUES (int4range(1,2), daterange(NULL,'2019-01-01'));
SELECT id, valid_at FROM r10tp3 ORDER BY id, valid_at;
DROP TABLE r10tp3;
