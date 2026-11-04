CREATE TABLE r10tu7(id int4range, valid_at daterange, x int);
INSERT INTO r10tu7 VALUES (int4range(1,2), daterange('2020-01-01','2020-06-01'), 1),
                          (int4range(1,2), daterange('2020-02-01','2020-03-01'), 2);
ALTER TABLE r10tu7 ADD CONSTRAINT r10tu7_u UNIQUE (id, valid_at WITHOUT OVERLAPS);
DELETE FROM r10tu7 WHERE x = 2;
ALTER TABLE r10tu7 ADD CONSTRAINT r10tu7_u UNIQUE (id, valid_at WITHOUT OVERLAPS);
SELECT conname, contype, conperiod FROM pg_constraint WHERE conrelid = 'r10tu7'::regclass ORDER BY conname;
DROP TABLE r10tu7;
