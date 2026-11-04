SELECT (1::float8 / 3)::text::float8 = 1::float8 / 3 AS roundtrips;
SELECT (0.1::float8 * 3)::text::float8 = 0.1::float8 * 3 AS roundtrips;
SELECT length((1::float8 / 3)::text);
SET extra_float_digits = 0;
SELECT (1::float8 / 3)::text::float8 = 1::float8 / 3 AS roundtrips;
RESET extra_float_digits;
