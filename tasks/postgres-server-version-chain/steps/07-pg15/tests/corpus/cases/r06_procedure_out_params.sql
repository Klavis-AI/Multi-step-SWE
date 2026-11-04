CREATE PROCEDURE x06_63p(IN a int, OUT b int) LANGUAGE plpgsql AS
  $$ BEGIN b := a * 2; END $$;
CALL x06_63p(3, NULL);
CALL x06_63p(0, NULL);
DROP PROCEDURE x06_63p(int, int);
SELECT count(*) FROM pg_proc WHERE proname='x06_63p';
