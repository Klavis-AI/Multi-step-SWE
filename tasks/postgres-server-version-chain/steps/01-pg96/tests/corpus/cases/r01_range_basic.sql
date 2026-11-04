SELECT int4range(1,5), int4range(1,5,'[]'), int8range(1,5), numrange(1.5,2.5);
SELECT daterange('2020-01-01','2020-06-01'), tsrange('2020-01-01','2020-06-01');
SELECT lower(int4range(1,5)), upper(int4range(1,5)), isempty(int4range(1,1));
SELECT lower_inc(int4range(1,5)), upper_inc(int4range(1,5,'[]')), lower_inf('(,5)'::int4range);
SELECT '[1,5)'::int4range, '(1,5]'::int4range, 'empty'::int4range;
