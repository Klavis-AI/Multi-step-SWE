SET extra_float_digits = 0;
SELECT 1::float8 / 3, 0.1::float8 * 3;
SET extra_float_digits = 3;
SELECT 1::float8 / 3, 0.1::float8 * 3;
SET extra_float_digits = -1;
SELECT 1::float8 / 3, 0.1::float8 * 3;
RESET extra_float_digits;
SELECT 1::float8 / 3;
