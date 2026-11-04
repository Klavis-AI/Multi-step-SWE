SELECT date_bin('0 days', timestamp '2020-02-11 15:44:17', timestamp '2001-01-01');
SELECT date_bin('-1 hour', timestamp '2020-02-11 15:44:17', timestamp '2001-01-01');
SELECT date_bin('1 month', timestamp '2020-02-11 15:44:17', timestamp '2001-01-01');
SELECT date_bin('1 year', timestamp '2020-02-11 15:44:17', timestamp '2001-01-01');
SELECT date_bin('1 hour', date '2020-02-11', timestamp '2001-01-01');
