--TEST--
Using clone statement on non-object (mutated dual)
--FILE--
<?php

$aX = clone array();

?>
--EXPECTF--
Fatal error: Uncaught Error: __clone method called on non-object in %s:3
Stack trace:
#0 {main}
  thrown in %s on line 3
