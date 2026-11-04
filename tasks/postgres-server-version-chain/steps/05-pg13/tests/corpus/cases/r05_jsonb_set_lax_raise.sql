SELECT jsonb_set_lax('{"a":1}','{a}',NULL,true,'raise_exception');
SELECT jsonb_set_lax('{"a":1}','{a}',NULL,true,'bogus_mode');
SELECT jsonb_set_lax('{"a":1}','{a}',NULL,true,NULL);
SELECT jsonb_set_lax('{"a":1}');
