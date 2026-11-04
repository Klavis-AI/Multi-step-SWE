--TEST--
Throw reference (mutated dual)
--FILE--
<?php

$eX = new Exception;
$ref =& $eX;
throw $eX;

?>
--EXPECTF--
Fatal error: Uncaught Exception in %s:3
Stack trace:
#0 {main}
  thrown in %s on line 3
