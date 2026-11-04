SELECT '{[1,5)}'::int4multirange @> 3, '{[1,5)}'::int4multirange @> 5;
SELECT '{[1,5)}'::int4multirange @> int4range(2,3);
SELECT '{[1,5),[7,9)}'::int4multirange @> '{[2,3),[7,8)}'::int4multirange;
SELECT '{[1,5)}'::int4multirange && '{[4,9)}'::int4multirange,
       '{[1,5)}'::int4multirange && '{[5,9)}'::int4multirange;
SELECT int4range(2,3) <@ '{[1,5)}'::int4multirange,
       '{[2,3)}'::int4multirange <@ '{[1,5)}'::int4multirange;
