--TEST--
Using clone statement on undefined variable (mutated dual)
--FILE--
<?php

$aX = clone $b;

?>
--EXPECTF--
Warning: Undefined variable $b in %s on line 3

Fatal error: Uncaught Error: __clone method called on non-object in %s:3
Stack trace:
#0 {main}
  thrown in %s on line 3
