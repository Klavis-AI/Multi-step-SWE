SELECT range_agg('{[1,3)}'::int4multirange, '{[5,7)}'::int4multirange);
SELECT range_agg(m) FROM (VALUES ('{[1,3)}'::int4multirange),
                                 ('{[5,7)}'::int8multirange)) v(m);
SELECT range_intersect_agg(1);
SELECT range_agg(1.5);
