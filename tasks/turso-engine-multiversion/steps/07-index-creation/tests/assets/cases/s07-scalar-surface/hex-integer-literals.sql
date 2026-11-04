-- R07.16: hexadecimal integer literals parse as integers, store with storage
-- class integer, and compare and arithmetically combine as their decimal
-- value.
CREATE TABLE h(id INTEGER, v INTEGER);
INSERT INTO h VALUES(1,0x10),(2,0xFF),(3,0x0),(4,0x7fffffff);
SELECT id||'='||v||' '||typeof(v) FROM h ORDER BY id;
SELECT 'literal='||0x10;
SELECT 'wide='||0xdeadbeef;
SELECT 'arith='||(0x10+0x01);
SELECT 'cmp='||count(*) FROM h WHERE v>=0xFF;
SELECT 'mixed='||count(*) FROM h WHERE v=255;
