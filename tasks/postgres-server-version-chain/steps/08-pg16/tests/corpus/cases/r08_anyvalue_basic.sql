SELECT any_value(x) FROM (VALUES (7),(7),(7)) v(x);
SELECT any_value(t) FROM (VALUES ('z'),('z')) v(t);
SELECT any_value(x) FROM (VALUES (1)) v(x) WHERE false;
SELECT any_value(NULL::int);
