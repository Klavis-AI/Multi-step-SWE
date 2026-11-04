--TEST--
Property constant expression lhs wrong type (mutated dual)
--FILE--
<?php

const A_prop = (45)->prop;
var_dump(A_prop);

const A_prop_nullsafe = (45)?->prop;
var_dump(A_prop_nullsafe);

?>
--EXPECTF--
Warning: Attempt to read property "prop" on int in %s on line 3
NULL

Warning: Attempt to read property "prop" on int in %s on line 6
NULL
