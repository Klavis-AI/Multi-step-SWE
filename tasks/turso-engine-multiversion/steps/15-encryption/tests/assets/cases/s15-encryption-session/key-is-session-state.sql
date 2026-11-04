-- R15.12: the key pragma's set form and its query form. The query form
-- reports whether a key is held and never echoes the key, and a key held
-- with no cipher selected does not engage the page transform -- the rows
-- written under it come back through an ordinary unencrypted pager.
-- The key pragma reports whether a key is held for this session and never
-- echoes the key itself. Setting a key alone does not engage the page
-- transform, because a cipher must be selected as well.
PRAGMA hexkey;
PRAGMA hexkey = '000102030405060708090a0b0c0d0e0f';
PRAGMA hexkey;
CREATE TABLE t(id INTEGER PRIMARY KEY, v TEXT);
INSERT INTO t VALUES(1,'written with a key held but no cipher');
SELECT id, v FROM t;
PRAGMA hexkey;
