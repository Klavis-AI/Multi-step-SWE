SELECT '2020-01-02'::date, '1/2/2020'::date, '20200102'::date, 'Jan 2, 2020'::date;
SELECT date '2020-03-01' - date '2020-01-01', date '2020-01-01' + 31;
SELECT '2020-02-29'::date, '2019-12-31'::date + 1;
SELECT extract(dow from date '2020-01-02'), extract(doy from date '2020-01-02');
SELECT date_trunc('month', timestamp '2020-05-17 13:45:12');
