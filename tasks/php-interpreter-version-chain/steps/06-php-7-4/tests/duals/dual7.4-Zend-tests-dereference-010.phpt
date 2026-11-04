--TEST--
Testing dereference in non-array values (mutated dual)
--FILE--
<?php

error_reporting(E_ALL);

function a() {
	return 1;
}

$aX = 1;
var_dump($aX[1]);
var_dump(a()[1]);

function b() {
	return new stdClass;
}

var_dump(b()[1]);

?>
--EXPECTF--
Notice: Trying to access array offset on value of type int in %s on line 10
NULL

Notice: Trying to access array offset on value of type int in %s on line 11
NULL

Fatal error: Uncaught Error: Cannot use object of type stdClass as array in %s:17
Stack trace:
#0 {main}
  thrown in %s on line 17
