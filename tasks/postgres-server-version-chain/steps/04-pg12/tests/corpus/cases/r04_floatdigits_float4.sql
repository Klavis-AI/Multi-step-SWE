SELECT (1::float4 / 3);
SELECT 0.1::float4;
SELECT 1e20::float4, 1e-20::float4;
SET extra_float_digits = 0;
SELECT (1::float4 / 3), 0.1::float4;
RESET extra_float_digits;
