-- lang_createtable.html, "ROWIDs and the INTEGER PRIMARY KEY": "If an UPDATE
-- statement attempts to set an integer primary key or rowid column to a NULL
-- or blob value, or to a string or real value that cannot be losslessly
-- converted to an integer, a "datatype mismatch" error occurs and the
-- statement is aborted."  reject-rowid-datatype.sql exercises the INSERT half
-- of the same paragraph; this is the UPDATE half.
CREATE TABLE t(a INTEGER PRIMARY KEY, b TEXT);
INSERT INTO t VALUES(1, 'p');
-- a string that can be losslessly converted to an integer is accepted
UPDATE t SET a = '7';
SELECT a, typeof(a), b FROM t;
-- NULL, a blob, and values that cannot be losslessly converted are rejected
UPDATE t SET a = NULL;
UPDATE t SET a = x'01';
UPDATE t SET a = 'zz';
UPDATE t SET a = 1.5;
SELECT a, typeof(a), b FROM t;
