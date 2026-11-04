SELECT extract(year from date '2020-05-06'), extract(month from date '2020-05-06');
SELECT extract(epoch from timestamptz '2020-01-01 00:00:00+00');
SELECT extract(hour from interval '3 days 04:05:06'), extract(day from interval '3 days 04:05:06');
SELECT pg_typeof(extract(year from date '2020-05-06'));
SELECT date_part('year', date '2020-05-06'), pg_typeof(date_part('year', date '2020-05-06'));
SELECT extract(second from timestamp '2020-01-02 03:04:05.5');
