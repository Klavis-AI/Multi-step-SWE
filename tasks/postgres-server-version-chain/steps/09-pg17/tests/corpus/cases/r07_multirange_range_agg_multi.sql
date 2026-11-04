SELECT range_agg(m) FROM (VALUES ('{[1,3)}'::int4multirange),
                                 ('{[5,7)}'::int4multirange)) v(m);
SELECT range_agg(m) FROM (VALUES ('{[1,5)}'::int4multirange),
                                 ('{[3,9)}'::int4multirange)) v(m);
SELECT range_agg(m) FROM (VALUES ('{}'::int4multirange)) v(m);
