SELECT '{[1,5)}'::int4multirange -|- '{[5,9)}'::int4multirange;
SELECT '{[1,5)}'::int4multirange << '{[7,9)}'::int4multirange,
       '{[7,9)}'::int4multirange >> '{[1,5)}'::int4multirange;
SELECT '{[1,5)}'::int4multirange &< '{[4,9)}'::int4multirange,
       '{[4,9)}'::int4multirange &> '{[1,5)}'::int4multirange;
SELECT '{[1,5),[7,9)}'::int4multirange <@ '{[0,12)}'::int4multirange;
