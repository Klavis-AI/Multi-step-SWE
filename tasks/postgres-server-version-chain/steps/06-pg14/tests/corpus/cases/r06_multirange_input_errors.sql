SELECT '{[5,1)}'::int4multirange;
SELECT '{[1,5)'::int4multirange;
SELECT '{[1,5),}'::int4multirange;
SELECT '[1,5)'::int4multirange;
SELECT 'nosuch'::int4multirange;
SELECT int4multirange(int8range(1,5));
