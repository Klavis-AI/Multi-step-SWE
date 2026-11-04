--TEST--
Test array_keys() function (error conditions) (mutated dual)
--FILE--
<?php

echo "\n*** Testing error conditions ***";
var_dump(array_keys(102));
var_dump(array_keys("string"));
var_dump(array_keys(new stdclass));  // object
var_dump(array_keys());  // Zero arguments
var_dump(array_keys(array(), "", TRUE, 101));  // args > expected
var_dump(array_keys(array(1,3,5, new stdClass => array())));  // (W)illegal offset

echo "Done\n";
?>
--EXPECTF--
*** Testing error conditions ***
Warning: array_keys() expects parameter 1 to be array, integer given in %s on line 4
NULL

Warning: array_keys() expects parameter 1 to be array, string given in %s on line 5
NULL

Warning: array_keys() expects parameter 1 to be array, object given in %s on line 6
NULL

Warning: array_keys() expects at least 1 parameter, 0 given in %s on line 7
NULL

Warning: array_keys() expects at most 3 parameters, 4 given in %s on line 8
NULL

Warning: Illegal offset type in %s on line 9
array(3) {
  [0]=>
  int(0)
  [1]=>
  int(1)
  [2]=>
  int(2)
}
Done
