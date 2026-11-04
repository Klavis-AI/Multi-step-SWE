-- TEST_INTERFACE.md section 3.3: the SQL function sqlite_version() returns the
-- release string this step targets.
SELECT sqlite_version();
SELECT sqlite_version() = '3.24.0';
WITH v(s) AS (SELECT sqlite_version()) SELECT s FROM v;
