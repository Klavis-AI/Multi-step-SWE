CREATE TABLE r10nn8(a int, b int, CONSTRAINT r10nn8_nn NOT NULL a);
ALTER TABLE r10nn8 ADD CONSTRAINT r10nn8_nn2 NOT NULL a;
ALTER TABLE r10nn8 ADD CONSTRAINT r10nn8_nn3 NOT NULL nosuchcol_r10;
CREATE TABLE r10nn9(a int, CONSTRAINT r10nn9_nn NOT NULL a NO INHERIT);
SELECT conname, contype, connoinherit FROM pg_constraint WHERE conrelid = 'r10nn9'::regclass ORDER BY conname;
DROP TABLE IF EXISTS r10nn9;
DROP TABLE r10nn8;
