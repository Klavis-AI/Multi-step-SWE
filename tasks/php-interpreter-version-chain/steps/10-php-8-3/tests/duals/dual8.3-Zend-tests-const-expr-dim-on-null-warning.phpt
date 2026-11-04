--TEST--
DIM on null in constant expr should emit warning (mutated dual)
--FILE--
<?php
const C = (null)['oof'];
var_dump(C);
?>
--EXPECTF--
Warning: Trying to access array offset on null in %s on line 2
NULL
