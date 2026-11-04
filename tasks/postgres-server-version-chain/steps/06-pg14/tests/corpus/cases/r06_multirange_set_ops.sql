SELECT '{[1,5)}'::int4multirange + '{[7,9)}'::int4multirange;
SELECT '{[1,5)}'::int4multirange + '{[4,9)}'::int4multirange;
SELECT '{[1,9)}'::int4multirange * '{[4,12)}'::int4multirange;
SELECT '{[1,9)}'::int4multirange - '{[4,6)}'::int4multirange;
SELECT '{[1,5)}'::int4multirange - '{[7,9)}'::int4multirange;
SELECT '{[1,5)}'::int4multirange * '{[7,9)}'::int4multirange;
