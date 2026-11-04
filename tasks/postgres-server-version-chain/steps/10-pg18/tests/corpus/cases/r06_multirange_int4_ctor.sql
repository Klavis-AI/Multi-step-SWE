SELECT int4multirange();
SELECT int4multirange(int4range(1,5));
SELECT int4multirange(int4range(1,5), int4range(7,9));
SELECT int4multirange(int4range(1,5), int4range(3,9));
SELECT '{}'::int4multirange, '{[1,5)}'::int4multirange, '{[1,5),[7,9)}'::int4multirange;
