CREATE TABLE r10tf7(id int4range, valid_at daterange, pid int4range,
   PRIMARY KEY (id, valid_at WITHOUT OVERLAPS),
   FOREIGN KEY (pid, PERIOD valid_at) REFERENCES r10tf7 (id, PERIOD valid_at));
INSERT INTO r10tf7 VALUES (int4range(1,2), daterange('2020-01-01','2021-01-01'), NULL);
INSERT INTO r10tf7 VALUES (int4range(2,3), daterange('2020-02-01','2020-05-01'), int4range(1,2));
INSERT INTO r10tf7 VALUES (int4range(3,4), daterange('2019-02-01','2019-05-01'), int4range(1,2));
SELECT id, valid_at, pid FROM r10tf7 ORDER BY id;
DROP TABLE r10tf7;
