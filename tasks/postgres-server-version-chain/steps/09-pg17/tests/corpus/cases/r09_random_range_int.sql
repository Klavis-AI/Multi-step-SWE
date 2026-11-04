SELECT random(1,1), random(5,5), random(-3,-3);
SELECT pg_typeof(random(1,2)), pg_typeof(random(1::int8,2::int8));
SELECT count(*) FROM (SELECT random(1,3) AS r FROM generate_series(1,30)) s WHERE r NOT BETWEEN 1 AND 3;
SELECT count(DISTINCT r) FROM (SELECT random(7,7) AS r FROM generate_series(1,10)) s;
