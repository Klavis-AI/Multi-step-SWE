PRAGMA user_version;
PRAGMA user_version = 42;
PRAGMA user_version;
PRAGMA encoding;
CREATE TABLE t(a);
PRAGMA user_version;
PRAGMA integrity_check;
-- PROFILE.md section 5: cache_size and synchronous must parse and run, the
-- value either one reports is never compared, and the assignment forms written
-- here "return no rows -- they write nothing at all to standard output".  A
-- query follows, proving the connection still works.
PRAGMA cache_size = 2000;
PRAGMA synchronous = FULL;
PRAGMA synchronous = NORMAL;
SELECT 'pragmas_ran';
