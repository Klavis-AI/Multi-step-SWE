SELECT EXTRACT(year FROM date '2020-05-06');
SELECT pg_typeof(EXTRACT(year FROM date '2020-05-06'));
SELECT EXTRACT(month FROM timestamp '2020-05-06 07:08:09');
SELECT EXTRACT(hour FROM interval '3 days 04:05:06');
SELECT EXTRACT(julian FROM date '2020-05-06');
