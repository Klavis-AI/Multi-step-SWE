SELECT int8multirange(int8range(1,5)), pg_typeof(int8multirange());
SELECT nummultirange(numrange(1.5,2.5)), pg_typeof(nummultirange());
SELECT datemultirange(daterange('2020-01-01','2020-02-01'));
SELECT tsmultirange(tsrange('2020-01-01','2020-02-01'));
SET TIME ZONE 'UTC';
SELECT tstzmultirange(tstzrange('2020-01-01 00:00+00','2020-02-01 00:00+00'));
