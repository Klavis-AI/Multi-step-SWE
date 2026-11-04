SELECT to_regtypemod('varchar(32)'), to_regtypemod('numeric(10,2)'), to_regtypemod('int');
SELECT format_type('varchar'::regtype, to_regtypemod('varchar(32)'));
SELECT format_type('numeric'::regtype, to_regtypemod('numeric(10,2)'));
SELECT to_regtypemod('nosuchtype_r09');
