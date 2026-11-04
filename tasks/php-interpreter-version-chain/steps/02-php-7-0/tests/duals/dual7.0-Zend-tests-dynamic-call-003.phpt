--TEST--
Testing dynamic call with invalid method name (mutated dual)
--FILE--
<?php 

$a = new stdClass;
$bX = 1;

$a::$bX();

?>
--EXPECTF--
Fatal error: Uncaught Error: Function name must be a string in %s:6
Stack trace:
#0 {main}
  thrown in %s on line 6
