CREATE TABLE r10ro3(id int primary key, v int);
INSERT INTO r10ro3 VALUES (1,10) RETURNING old.v, new.v;
INSERT INTO r10ro3 VALUES (2,20) RETURNING old.*, new.*;
INSERT INTO r10ro3 VALUES (3,30) RETURNING old.id IS NULL AS old_null, new.id;
SELECT id, v FROM r10ro3 ORDER BY id;
DROP TABLE r10ro3;
