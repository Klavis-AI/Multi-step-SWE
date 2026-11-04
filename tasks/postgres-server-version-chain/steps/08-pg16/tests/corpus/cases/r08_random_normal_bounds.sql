SELECT random_normal(0,0), random_normal(5,0), random_normal(-2.5,0);
SELECT pg_typeof(random_normal()), pg_typeof(random_normal(1,1));
SELECT count(*) FROM (SELECT random_normal(0,1) AS r FROM generate_series(1,20)) s WHERE r IS NULL;
SELECT count(*) FROM (SELECT random_normal(100,0) AS r FROM generate_series(1,10)) s WHERE r <> 100;
