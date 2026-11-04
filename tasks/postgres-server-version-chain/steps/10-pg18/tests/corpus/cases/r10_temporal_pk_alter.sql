CREATE TABLE r10tpb(id int4range, valid_at daterange);
INSERT INTO r10tpb VALUES (int4range(1,2), daterange('2020-01-01','2020-06-01'));
ALTER TABLE r10tpb ADD CONSTRAINT r10tpb_pk PRIMARY KEY (id, valid_at WITHOUT OVERLAPS);
INSERT INTO r10tpb VALUES (int4range(1,2), daterange('2020-02-01','2020-03-01'));
SELECT conname, contype, conperiod FROM pg_constraint WHERE conrelid = 'r10tpb'::regclass ORDER BY conname;
ALTER TABLE r10tpb DROP CONSTRAINT r10tpb_pk;
INSERT INTO r10tpb VALUES (int4range(1,2), daterange('2020-02-01','2020-03-01'));
SELECT count(*) FROM r10tpb;
DROP TABLE r10tpb;
