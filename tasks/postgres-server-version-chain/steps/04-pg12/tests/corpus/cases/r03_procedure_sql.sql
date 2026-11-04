CREATE TABLE q3_prs_t(i int);
CREATE PROCEDURE q3_prs(x int) LANGUAGE sql AS $$ INSERT INTO q3_prs_t VALUES (x) $$;
CALL q3_prs(1);
CALL q3_prs(2);
SELECT i FROM q3_prs_t ORDER BY i;
DROP PROCEDURE q3_prs(int);
DROP TABLE q3_prs_t;
