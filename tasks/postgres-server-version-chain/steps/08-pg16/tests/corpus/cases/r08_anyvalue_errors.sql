SELECT any_value(*) FROM (VALUES (1)) v(x);
SELECT any_value();
SELECT any_value(1,2);
SELECT any_value(x) FROM (VALUES (1)) v(x) WHERE any_value(x) > 0;
SELECT proname, prokind FROM pg_proc WHERE proname = 'any_value' ORDER BY 1;
