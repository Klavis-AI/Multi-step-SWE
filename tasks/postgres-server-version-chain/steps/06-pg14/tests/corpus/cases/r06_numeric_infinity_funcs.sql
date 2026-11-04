SELECT abs('-Infinity'::numeric), sign('Infinity'::numeric);
SELECT round('Infinity'::numeric), trunc('Infinity'::numeric);
SELECT 'Infinity'::numeric::float8, 'Infinity'::float8::numeric;
SELECT 'NaN'::numeric > 'Infinity'::numeric;
SELECT sqrt('Infinity'::numeric);
