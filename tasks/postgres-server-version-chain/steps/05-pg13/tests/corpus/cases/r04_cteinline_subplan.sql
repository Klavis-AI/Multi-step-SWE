CREATE SEQUENCE q4_cip_seq;
CREATE FUNCTION q4_cip_tick(x int) RETURNS int LANGUAGE plpgsql STABLE AS $$
BEGIN PERFORM nextval('q4_cip_seq'); RETURN x; END $$;
WITH w AS (SELECT q4_cip_tick(1) AS n)
  SELECT (SELECT n FROM w WHERE g > 0) AS v FROM generate_series(1,3) g ORDER BY 1;
SELECT last_value, is_called FROM q4_cip_seq;
DROP FUNCTION q4_cip_tick(int);
DROP SEQUENCE q4_cip_seq;
