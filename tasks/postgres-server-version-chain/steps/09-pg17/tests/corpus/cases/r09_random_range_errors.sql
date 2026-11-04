SELECT random(10,1);
SELECT random(1.5::numeric, 0.5::numeric);
SELECT random(NULL::int, 5) IS NULL;
SELECT random('NaN'::numeric, 'NaN'::numeric);
