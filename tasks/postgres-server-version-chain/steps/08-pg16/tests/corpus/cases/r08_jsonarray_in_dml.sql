CREATE TABLE r08ja1(id int, doc json);
INSERT INTO r08ja1 VALUES (1, JSON_ARRAY(1,2)), (2, JSON_ARRAY('a',NULL ABSENT ON NULL));
SELECT id, doc FROM r08ja1 ORDER BY id;
SELECT id, json_array_length(doc) FROM r08ja1 ORDER BY id;
DROP TABLE r08ja1;
