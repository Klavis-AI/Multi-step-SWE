SELECT range_agg(r) FROM (VALUES (int4range(1,5)),(int4range(7,9))) v(r);
SELECT range_agg(r) FROM (VALUES (int4range(1,5)),(int4range(3,9))) v(r);
SELECT range_agg(r ORDER BY r DESC) FROM (VALUES (int4range(7,9)),(int4range(1,5))) v(r);
SELECT range_agg(r) FROM (VALUES (int4range(1,5)),(NULL::int4range)) v(r);
SELECT range_agg(r) FROM (SELECT int4range(1,2) WHERE false) t(r);
