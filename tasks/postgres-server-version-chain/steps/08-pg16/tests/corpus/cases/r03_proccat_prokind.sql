CREATE PROCEDURE q3_pck_p(x int) LANGUAGE plpgsql AS $$ BEGIN NULL; END $$;
CREATE FUNCTION q3_pck_f(x int) RETURNS int LANGUAGE sql AS $$ SELECT x $$;
SELECT proname, prokind, prorettype::regtype::text FROM pg_proc
  WHERE proname IN ('q3_pck_p','q3_pck_f') ORDER BY proname;
SELECT prokind FROM pg_proc WHERE proname = 'count' ORDER BY prokind;
SELECT DISTINCT prokind FROM pg_proc WHERE proname IN ('rank','row_number') ORDER BY 1;
DROP PROCEDURE q3_pck_p(int);
DROP FUNCTION q3_pck_f(int);
