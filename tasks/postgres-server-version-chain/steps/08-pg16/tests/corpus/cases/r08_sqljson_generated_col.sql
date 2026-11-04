CREATE TABLE r08sg1(id int, k text, doc jsonb GENERATED ALWAYS AS (JSON_OBJECT('k': k RETURNING jsonb)) STORED);
INSERT INTO r08sg1(id,k) VALUES (1,'x'),(2,NULL);
SELECT id, doc FROM r08sg1 ORDER BY id;
SELECT column_name, generation_expression FROM information_schema.columns WHERE table_name='r08sg1' ORDER BY ordinal_position;
DROP TABLE r08sg1;
