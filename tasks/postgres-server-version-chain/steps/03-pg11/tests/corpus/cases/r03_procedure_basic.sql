CREATE PROCEDURE q3_prb(x int) LANGUAGE plpgsql AS $$
BEGIN RAISE NOTICE 'q3_prb called with %', x; END $$;
CALL q3_prb(7);
CALL q3_prb(-1);
DROP PROCEDURE q3_prb(int);
CALL q3_prb(1);
