SELECT x, any_value(x) OVER (ORDER BY x) FROM (VALUES (5),(6)) v(x) ORDER BY x;
SELECT x, any_value(x) OVER () FROM (VALUES (5)) v(x);
SELECT any_value(x) FILTER (WHERE x > 1) FROM (VALUES (1),(2),(2)) v(x);
SELECT any_value(DISTINCT x) FROM (VALUES (3),(3)) v(x);
