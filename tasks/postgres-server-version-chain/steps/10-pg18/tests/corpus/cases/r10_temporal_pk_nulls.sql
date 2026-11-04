CREATE TABLE r10tp4(id int4range, valid_at daterange, PRIMARY KEY (id, valid_at WITHOUT OVERLAPS));
INSERT INTO r10tp4 VALUES (NULL, daterange('2020-01-01','2020-06-01'));
INSERT INTO r10tp4 VALUES (int4range(1,2), NULL);
SELECT count(*) FROM r10tp4;
SELECT conname, contype, conperiod FROM pg_constraint WHERE conrelid = 'r10tp4'::regclass ORDER BY conname;
DROP TABLE r10tp4;
