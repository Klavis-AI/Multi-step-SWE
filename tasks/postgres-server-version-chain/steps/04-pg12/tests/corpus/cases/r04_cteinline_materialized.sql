CREATE SEQUENCE q4_cim_seq;
CREATE FUNCTION q4_cim_tick(x int) RETURNS int LANGUAGE plpgsql STABLE AS $$
BEGIN PERFORM nextval('q4_cim_seq'); RETURN x; END $$;
WITH w AS MATERIALIZED (SELECT q4_cim_tick(g) AS n FROM generate_series(1,4) g)
  SELECT count(*) FROM w;
SELECT last_value, is_called FROM q4_cim_seq;
DROP FUNCTION q4_cim_tick(int);
DROP SEQUENCE q4_cim_seq;
