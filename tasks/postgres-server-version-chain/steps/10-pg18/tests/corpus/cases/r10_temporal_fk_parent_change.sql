CREATE TABLE r10tf3_p(id int4range, valid_at daterange, PRIMARY KEY (id, valid_at WITHOUT OVERLAPS));
INSERT INTO r10tf3_p VALUES (int4range(1,2), daterange('2020-01-01','2021-01-01'));
CREATE TABLE r10tf3_c(id int, valid_at daterange, pid int4range,
   FOREIGN KEY (pid, PERIOD valid_at) REFERENCES r10tf3_p (id, PERIOD valid_at));
INSERT INTO r10tf3_c VALUES (1, daterange('2020-02-01','2020-04-01'), int4range(1,2));
DELETE FROM r10tf3_p;
UPDATE r10tf3_p SET valid_at = daterange('2020-06-01','2021-01-01');
UPDATE r10tf3_p SET valid_at = daterange('2019-01-01','2021-01-01');
SELECT id, valid_at FROM r10tf3_p ORDER BY id, valid_at;
DROP TABLE r10tf3_c;
DROP TABLE r10tf3_p;
