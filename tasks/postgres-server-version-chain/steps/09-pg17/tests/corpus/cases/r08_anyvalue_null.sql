SELECT any_value(x) FROM (VALUES (NULL::int),(NULL::int)) v(x);
SELECT any_value(x) IS NULL FROM (VALUES (NULL::int)) v(x);
SELECT any_value(x ORDER BY x NULLS FIRST) FROM (VALUES (2),(NULL),(1)) v(x);
SELECT any_value(x ORDER BY x NULLS LAST) FROM (VALUES (2),(NULL),(1)) v(x);
