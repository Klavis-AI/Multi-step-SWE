SELECT jsonb_set_lax('{"a":1,"b":2}','{a}','9'::jsonb);
SELECT jsonb_set_lax('{"a":1,"b":2}','{a}',NULL,true,'use_json_null');
SELECT jsonb_set_lax('{"a":1,"b":2}','{a}',NULL,true,'delete_key');
SELECT jsonb_set_lax('{"a":1,"b":2}','{a}',NULL,true,'return_target');
