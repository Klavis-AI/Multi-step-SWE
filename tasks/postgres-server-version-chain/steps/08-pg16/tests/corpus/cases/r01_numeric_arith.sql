SELECT 1.1+2.2, 1.1*2.2, 10.0/4, 10/4.0, 1/3::numeric;
SELECT 100.5::numeric % 7;
SELECT 2::numeric ^ 10, sqrt(2::numeric);
SELECT trunc(2.7::numeric), trunc(-2.7::numeric), ceil(2.1::numeric), floor(2.9::numeric);
SELECT round(2.5::numeric), round(-2.5::numeric), round(2.345::numeric,2), round(1.005::numeric,2);
