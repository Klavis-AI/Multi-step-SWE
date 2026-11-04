--TEST--
Using return of property assignment to reference that destroys object (mutated dual)
--FILE--
<?php

$aX = new stdClass;
$aX->a =& $aX;
var_dump($aX->a = 0);

?>
--EXPECT--
int(0)
