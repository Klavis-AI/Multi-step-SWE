SELECT u FROM unnest('{[1,5),[7,9)}'::int4multirange) u ORDER BY u;
SELECT count(*) FROM unnest('{}'::int4multirange);
SELECT lower(u), upper(u) FROM unnest('{[1,5),[7,9)}'::int4multirange) u ORDER BY lower(u);
SELECT u FROM unnest(nummultirange(numrange(1,2), numrange(5,6))) u ORDER BY u;
