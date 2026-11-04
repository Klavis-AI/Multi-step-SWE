SELECT 1, 1., .1, 1.0, 1e3, 1.5e-3, -1, - 1;
SELECT pg_typeof(1), pg_typeof(1.0), pg_typeof(1e3), pg_typeof(2147483648);
SELECT 0.1+0.2 = 0.3, 0.1::float8+0.2::float8 = 0.3::float8;
