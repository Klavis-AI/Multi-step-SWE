CREATE TABLE r08sv1(id int, k text, v int);
INSERT INTO r08sv1 VALUES (1,'a',10),(2,'b',20);
CREATE VIEW r08sv1v AS SELECT id, JSON_OBJECT('k': k, 'v': v RETURNING jsonb) AS doc FROM r08sv1;
SELECT id, doc FROM r08sv1v ORDER BY id;
SELECT pg_get_viewdef('r08sv1v'::regclass, true);
DROP VIEW r08sv1v;
DROP TABLE r08sv1;
