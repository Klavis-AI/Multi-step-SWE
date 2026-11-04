CREATE TABLE r10ro7(id int primary key, v int, t text);
INSERT INTO r10ro7 VALUES (1,10,'a');
UPDATE r10ro7 SET v = 11 WHERE id = 1 RETURNING old.*;
UPDATE r10ro7 SET v = 12 WHERE id = 1 RETURNING new.*;
UPDATE r10ro7 SET v = 13 WHERE id = 1 RETURNING *, old.v;
UPDATE r10ro7 SET v = 14 WHERE id = 1 RETURNING r10ro7.*;
DROP TABLE r10ro7;
