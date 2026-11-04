SELECT EXTRACT(year FROM date '2020-05-06') / 3;
SELECT EXTRACT(year FROM date '2020-05-06') + 0.5;
SELECT round(EXTRACT(epoch FROM interval '1.5 hours'), 3);
SELECT EXTRACT(day FROM date '2020-05-06')::int;
