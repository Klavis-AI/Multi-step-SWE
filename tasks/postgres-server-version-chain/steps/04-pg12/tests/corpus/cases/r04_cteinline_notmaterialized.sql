CREATE SEQUENCE q4_cin_seq;
CREATE FUNCTION q4_cin_tick(x int) RETURNS int LANGUAGE plpgsql STABLE AS $$
BEGIN PERFORM nextval('q4_cin_seq'); RETURN x; END $$;
WITH w AS NOT MATERIALIZED (SELECT q4_cin_tick(g) AS n FROM generate_series(1,4) g)
  SELECT (SELECT count(*) FROM w) + (SELECT count(*) FROM w) AS total;
SELECT last_value, is_called FROM q4_cin_seq;
DROP FUNCTION q4_cin_tick(int);
DROP SEQUENCE q4_cin_seq;
