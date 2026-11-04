CREATE PROCEDURE x06_64p(IN a int, OUT b int, INOUT c int) LANGUAGE plpgsql AS
  $$ BEGIN b := a * 2; c := c + 1; END $$;
CALL x06_64p(3, NULL, 10);
SELECT proname, pronargs, proargmodes FROM pg_proc WHERE proname='x06_64p';
SELECT pg_get_function_arguments(oid) FROM pg_proc WHERE proname='x06_64p';
DROP PROCEDURE x06_64p;
