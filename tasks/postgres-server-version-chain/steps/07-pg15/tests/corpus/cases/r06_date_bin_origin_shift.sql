SELECT date_bin('1 hour', timestamp '2020-02-11 15:44:17', timestamp '2020-02-11 00:30:00');
SELECT date_bin('1 day', timestamp '2020-02-11 15:44:17', timestamp '2020-02-11 15:44:17');
SELECT date_bin('1 day', timestamp '2020-02-11 15:44:17', timestamp '2030-01-01');
SELECT date_bin('7 days', timestamp '2020-02-11 15:44:17', timestamp '2001-01-01');
SELECT date_bin('1 hour', timestamp '1999-01-01 05:30:00', timestamp '2001-01-01');
