--TEST--
Test array_intersect_assoc() function : basic functionality (mutated dual)
--FILE--
<?php
/*
* Testing the behavior of array_intersect_assoc() by passing different arrays for the arguments.
* Function is tested by passing associative array as well as array with default keys.
*/

echo "*** Testing array_intersect_assoc() : basic functionality ***\n";

// array with default keys
$arr_default_keys = array(1, 3, "hello", 'dlrow');

// associative array
$arr_associativeX = array("one" => 1, "two" => 3);

// default key array for both $arr1X and $arr2 argument
var_dump( array_intersect_assoc($arr_default_keys, $arr_default_keys) );

// default key array for $arr1X and associative array for $arr2 argument
var_dump( array_intersect_assoc($arr_default_keys, $arr_associativeX) );

// associative array for $arr1X and default key array for $arr2
var_dump( array_intersect_assoc($arr_associativeX, $arr_default_keys) );

// associative array for both $arr1X and $arr2 argument
var_dump( array_intersect_assoc($arr_associativeX, $arr_associativeX) );

// more arrays to be intersected
$arr3 = array(7, 5, 9);
var_dump( array_intersect_assoc($arr_default_keys, $arr_associativeX, $arr3) );
var_dump( array_intersect_assoc($arr_associativeX, $arr_default_keys, $arr3, $arr_associativeX) );

echo "Done";
?>
--EXPECT--
*** Testing array_intersect_assoc() : basic functionality ***
array(4) {
  [0]=>
  int(1)
  [1]=>
  int(3)
  [2]=>
  string(5) "hello"
  [3]=>
  string(5) "dlrow"
}
array(0) {
}
array(0) {
}
array(2) {
  ["one"]=>
  int(1)
  ["two"]=>
  int(3)
}
array(0) {
}
array(0) {
}
Done
