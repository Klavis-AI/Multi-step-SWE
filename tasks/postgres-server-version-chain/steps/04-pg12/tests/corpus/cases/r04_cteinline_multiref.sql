CREATE SEQUENCE q4_cir_seq;
CREATE FUNCTION q4_cir_tick(x int) RETURNS int LANGUAGE plpgsql STABLE AS $$
BEGIN PERFORM nextval('q4_cir_seq'); RETURN x; END $$;
WITH w AS (SELECT q4_cir_tick(g) AS n FROM generate_series(1,4) g)
  SELECT (SELECT count(*) FROM w) + (SELECT count(*) FROM w) AS total;
SELECT last_value, is_called FROM q4_cir_seq;
DROP FUNCTION q4_cir_tick(int);
DROP SEQUENCE q4_cir_seq;
