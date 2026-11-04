SELECT round(erf(0)::numeric,6), round(erf(1)::numeric,6), round(erf(-1)::numeric,6);
SELECT round(erfc(0)::numeric,6), round(erfc(1)::numeric,6);
SELECT erf('NaN'::float8), erf('Infinity'::float8), erfc('-Infinity'::float8);
SELECT round((erf(0.5)+erfc(0.5))::numeric,10);
