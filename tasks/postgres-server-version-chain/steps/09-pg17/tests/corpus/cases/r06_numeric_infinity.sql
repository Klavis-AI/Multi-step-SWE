SELECT 'Infinity'::numeric, '-Infinity'::numeric, 'inf'::numeric;
SELECT 'Infinity'::numeric + 1, 'Infinity'::numeric * 2;
SELECT 'Infinity'::numeric > 1e100::numeric, '-Infinity'::numeric < 0;
SELECT 'Infinity'::numeric - 'Infinity'::numeric;
