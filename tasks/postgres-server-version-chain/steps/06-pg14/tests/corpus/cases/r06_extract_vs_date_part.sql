SELECT date_part('year', date '2020-05-06'), pg_typeof(date_part('year', date '2020-05-06'));
SELECT EXTRACT(year FROM date '2020-05-06') = date_part('year', date '2020-05-06');
SELECT date_part('epoch', timestamptz '2020-01-01 00:00:00+00');
SELECT EXTRACT(epoch FROM timestamptz '2020-01-01 00:00:00+00');
