--TEST--
Using clone statement on undefined variable (mutated dual)
--FILE--
<?php

$aX = clone $b;

?>
--EXPECTF--
Notice: Undefined variable: b in %s on line 3

Fatal error: __clone method called on non-object in %s on line 3
