SELECT int4range(1,5)::int4multirange;
SELECT multirange(int4range(1,5));
SELECT range_merge('{[1,3),[7,9)}'::int4multirange);
SELECT range_merge('{}'::int4multirange);
SELECT '{[1,5)}'::int4multirange = '{[1,5)}'::int4multirange,
       '{[1,5)}'::int4multirange < '{[2,5)}'::int4multirange;
