-- R07.17: likely() and likelihood() parse and return their first argument
-- unchanged, so wrapping a predicate in one cannot change which rows come
-- back.
--
-- Both are planner hints: they may inform a plan but they may not change one
-- row of its answer.
CREATE TABLE p(id INTEGER, v INTEGER);
INSERT INTO p VALUES(1,10),(2,20),(3,30),(4,40);
SELECT 'likely='||likely(1);
SELECT 'likelihood='||likelihood(9,0.5);
SELECT 'likelihood-type='||typeof(likelihood(9,0.5));
SELECT 'plain='||count(*) FROM p WHERE v>15;
SELECT 'hinted='||count(*) FROM p WHERE likely(v>15);
SELECT 'weighted='||count(*) FROM p WHERE likelihood(v>15,0.25);
SELECT 'rows:'||id FROM p WHERE likely(v>=30) ORDER BY id;
