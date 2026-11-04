SELECT range_intersect_agg(r) FROM (VALUES (int4range(1,9)),(int4range(4,12))) v(r);
SELECT range_intersect_agg(r) FROM (VALUES (int4range(1,3)),(int4range(5,7))) v(r);
SELECT range_intersect_agg(r) FROM (VALUES (int4range(1,9))) v(r);
SELECT range_intersect_agg(r) FROM (SELECT int4range(1,2) WHERE false) t(r);
