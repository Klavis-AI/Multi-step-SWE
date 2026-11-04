CREATE TABLE r10roa(id int primary key, "old" int, "new" int);
INSERT INTO r10roa VALUES (1,2,3);
UPDATE r10roa SET id = id RETURNING "old", "new";
UPDATE r10roa SET id = id RETURNING old.id, old."old";
UPDATE r10roa SET id = id RETURNING old.*, new.*;
DROP TABLE r10roa;
