SELECT random(1.5::numeric, 1.5::numeric);
SELECT pg_typeof(random(1.0, 2.0));
SELECT count(*) FROM (SELECT random(0.0, 1.0) AS r FROM generate_series(1,20)) s WHERE r < 0 OR r > 1;
SELECT random(2.500::numeric, 2.500::numeric);
