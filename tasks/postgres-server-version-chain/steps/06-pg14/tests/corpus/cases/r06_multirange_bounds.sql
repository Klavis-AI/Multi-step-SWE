SELECT lower('{[1,5),[7,9)}'::int4multirange), upper('{[1,5),[7,9)}'::int4multirange);
SELECT lower('{}'::int4multirange) IS NULL, upper('{}'::int4multirange) IS NULL;
SELECT isempty('{}'::int4multirange), isempty('{[1,2)}'::int4multirange);
SELECT lower_inf(nummultirange(numrange(NULL,2.5))), upper_inf(nummultirange(numrange(1,NULL)));
SELECT nummultirange(numrange(NULL,2.5)), nummultirange(numrange(1,NULL));
