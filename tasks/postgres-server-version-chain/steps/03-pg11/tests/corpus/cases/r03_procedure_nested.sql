CREATE TABLE q3_pns(i int);
CREATE PROCEDURE q3_pns_inner(x int) LANGUAGE plpgsql AS $$
BEGIN INSERT INTO q3_pns VALUES (x); END $$;
CREATE PROCEDURE q3_pns_outer() LANGUAGE plpgsql AS $$
BEGIN CALL q3_pns_inner(1); CALL q3_pns_inner(2); END $$;
CALL q3_pns_outer();
SELECT i FROM q3_pns ORDER BY i;
DO $$ BEGIN CALL q3_pns_inner(3); END $$;
SELECT i FROM q3_pns ORDER BY i;
DROP PROCEDURE q3_pns_outer();
DROP PROCEDURE q3_pns_inner(int);
DROP TABLE q3_pns;
