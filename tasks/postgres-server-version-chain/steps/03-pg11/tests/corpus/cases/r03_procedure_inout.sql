CREATE PROCEDURE q3_pio_p(a int, INOUT b int) LANGUAGE plpgsql AS $$
BEGIN b := a + b; END $$;
CALL q3_pio_p(3, 4);
CALL q3_pio_p(0, 0);
CREATE PROCEDURE q3_pio_ret(a int) LANGUAGE plpgsql AS $$ BEGIN RETURN 1; END $$;
DROP PROCEDURE q3_pio_p(int, int);
