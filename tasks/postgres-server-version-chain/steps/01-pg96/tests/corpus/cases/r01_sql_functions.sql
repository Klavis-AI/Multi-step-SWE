CREATE FUNCTION add2(a int, b int) RETURNS int LANGUAGE SQL AS $$ SELECT a + b $$;
SELECT add2(1,2), add2(-1,1);
CREATE OR REPLACE FUNCTION add2(a int, b int) RETURNS int LANGUAGE SQL AS $$ SELECT a * b $$;
SELECT add2(3,4);
SELECT routine_name, routine_type FROM information_schema.routines WHERE routine_name='add2';
DROP FUNCTION add2(int,int);
SELECT add2(1,2);
