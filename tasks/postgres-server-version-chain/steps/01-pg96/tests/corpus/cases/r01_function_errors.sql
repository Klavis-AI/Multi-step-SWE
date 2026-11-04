SELECT nosuch_function_r01(1);
SELECT length(1,2,3);
CREATE FUNCTION bad1() RETURNS int LANGUAGE nosuchlang AS $$ SELECT 1 $$;
