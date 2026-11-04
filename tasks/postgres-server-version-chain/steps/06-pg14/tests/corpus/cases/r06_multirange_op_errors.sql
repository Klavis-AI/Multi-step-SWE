SELECT '{[1,5)}'::int4multirange + int4range(7,9);
SELECT '{[1,5)}'::int4multirange + '{[7,9)}'::int8multirange;
SELECT '{[1,5)}'::int4multirange @> 'x';
SELECT range_agg(1);
SELECT unnest('{[1,5)}'::int4multirange, 1);
