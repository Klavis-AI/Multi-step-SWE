--TEST--
Testing dynamic call with invalid value for method name (mutated dual)
--FILE--
<?php 

$aX = new stdClass;

$aX::$aX();

?>
--EXPECTF--
Fatal error: Uncaught Error: Function name must be a string in %s:5
Stack trace:
#0 {main}
  thrown in %s on line 5
