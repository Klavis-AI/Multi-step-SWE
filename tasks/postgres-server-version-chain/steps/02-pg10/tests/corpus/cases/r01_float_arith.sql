SELECT 1::float8/3, 2::float8/3, 10.0::float8/4;
SELECT 0.1::float4, 1e40::float8, 1e-40::float8;
SELECT 'NaN'::float8, 'Infinity'::float8, '-Infinity'::float8;
SELECT 'NaN'::float8 > 1e308, 'Infinity'::float8 > 1e308;
SELECT sqrt(2::float8), exp(1::float8), ln(10::float8), log(100::float8);
