SELECT div(9::numeric, 4), div(-9::numeric, 4), div(9::numeric, -4);
SELECT div(9.9::numeric, 2.2), 9.9::numeric / 2.2;
SELECT div(1::numeric, 3), mod(9::numeric, 4);
SELECT div('NaN'::numeric, 2);
