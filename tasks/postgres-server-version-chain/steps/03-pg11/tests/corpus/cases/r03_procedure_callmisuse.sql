CREATE FUNCTION q3_pcm_f(x int) RETURNS int LANGUAGE sql AS $$ SELECT x + 1 $$;
CREATE PROCEDURE q3_pcm_p(x int) LANGUAGE plpgsql AS $$ BEGIN NULL; END $$;
CALL q3_pcm_f(1);
SELECT q3_pcm_p(1);
CALL q3_pcm_p(1);
SELECT q3_pcm_f(1);
CALL q3_pcm_nosuch(1);
DROP FUNCTION q3_pcm_f(int);
DROP PROCEDURE q3_pcm_p(int);
