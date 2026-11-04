SELECT 3.7::int, 3.2::int, (-3.7)::int, (-3.2)::int, 2.5::int, 3.5::int;
SELECT 3.7::float8::int, 1e18::float8::int8;
SELECT 42::int::text, 42::text::int;
SELECT '1'::int = 1, 1::int8 = 1::int2;
