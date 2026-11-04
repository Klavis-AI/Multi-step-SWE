CREATE TABLE x06_27a(id int, j jsonb);
INSERT INTO x06_27a VALUES (1,'[1,2,3]'),(2,'[1,2,3]'),(3,'[1,2,3]');
UPDATE x06_27a SET j[0] = '9' WHERE id = 1;
UPDATE x06_27a SET j[5] = '9' WHERE id = 2;
UPDATE x06_27a SET j[-1] = '9' WHERE id = 3;
SELECT id, j FROM x06_27a ORDER BY id;
DROP TABLE x06_27a;
