CREATE TABLE x06_14a(id int, m int4multirange);
INSERT INTO x06_14a VALUES (1,'{[1,5)}'),(2,'{[7,9),[20,30)}'),(3,'{}');
CREATE INDEX x06_14i ON x06_14a USING gist (m);
SELECT id, m FROM x06_14a ORDER BY id;
SELECT id FROM x06_14a WHERE m @> 8 ORDER BY id;
SELECT id FROM x06_14a WHERE m && '{[4,8)}'::int4multirange ORDER BY id;
SELECT range_agg(r) FROM (SELECT unnest(m) r FROM x06_14a) s;
DROP TABLE x06_14a;
