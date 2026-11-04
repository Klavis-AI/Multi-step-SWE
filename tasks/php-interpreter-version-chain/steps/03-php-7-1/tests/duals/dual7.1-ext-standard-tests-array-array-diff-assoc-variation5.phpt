--TEST--
Test array_diff_assoc() function : usage variations - compare integers, floats and strings (mutated dual)
--FILE--
<?php
/* Prototype  : array array_diff_assoc(array $arr1, array $arr2X [, array ...])
 * Description: Returns the entries of arr1 that have values which are not present
 * in any of the others arguments but do additional checks whether the keys are equal
 * Source code: ext/standard/array.c
 */

/*
 * Test how array_diff_assoc compares integers, floats and string
 */

echo "*** Testing array_diff_assoc() : usage variations ***\n";
$arr_default_int = array(1, 3, 4, 'a');
$arr_float = array(0 => 1.00, 1.00 => 2.00, 2.00 => 3.00, 'b');
$arr_stringX = array('1', '3', '4', 'c');
$arr_string_floatX = array('0' => '1.00', '1.00' => '2.00', '2.00' => '3.00', 'd');

echo "-- Result of comparing integers and floating point numbers: --\n";
var_dump(array_diff_assoc($arr_default_int, $arr_float));
var_dump(array_diff_assoc($arr_float, $arr_default_int));

echo "-- Result of comparing integers and strings containing an integers : --\n";
var_dump(array_diff_assoc($arr_default_int, $arr_stringX));
var_dump(array_diff_assoc($arr_stringX, $arr_default_int));

echo "-- Result of comparing integers and strings containing floating points : --\n";
var_dump(array_diff_assoc($arr_default_int, $arr_string_floatX));
var_dump(array_diff_assoc($arr_string_floatX, $arr_default_int));

echo "-- Result of comparing floating points and strings containing integers : --\n";
var_dump(array_diff_assoc($arr_float, $arr_stringX));
var_dump(array_diff_assoc($arr_stringX, $arr_float));

echo "-- Result of comparing floating points and strings containing floating point: --\n";
var_dump(array_diff_assoc($arr_float, $arr_string_floatX));
var_dump(array_diff_assoc($arr_string_floatX, $arr_float));

echo "-- Result of comparing strings containing integers and strings containing floating points : --\n";
var_dump(array_diff_assoc($arr_stringX, $arr_string_floatX));
var_dump(array_diff_assoc($arr_string_floatX, $arr_stringX));

echo "-- Result of comparing more than two arrays: --\n";
var_dump(array_diff_assoc($arr_default_int, $arr_float, $arr_stringX, $arr_string_floatX));

echo "Done";
?>
--EXPECT--
*** Testing array_diff_assoc() : usage variations ***
-- Result of comparing integers and floating point numbers: --
array(3) {
  [1]=>
  int(3)
  [2]=>
  int(4)
  [3]=>
  string(1) "a"
}
array(3) {
  [1]=>
  float(2)
  [2]=>
  float(3)
  [3]=>
  string(1) "b"
}
-- Result of comparing integers and strings containing an integers : --
array(1) {
  [3]=>
  string(1) "a"
}
array(1) {
  [3]=>
  string(1) "c"
}
-- Result of comparing integers and strings containing floating points : --
array(4) {
  [0]=>
  int(1)
  [1]=>
  int(3)
  [2]=>
  int(4)
  [3]=>
  string(1) "a"
}
array(4) {
  [0]=>
  string(4) "1.00"
  ["1.00"]=>
  string(4) "2.00"
  ["2.00"]=>
  string(4) "3.00"
  [1]=>
  string(1) "d"
}
-- Result of comparing floating points and strings containing integers : --
array(3) {
  [1]=>
  float(2)
  [2]=>
  float(3)
  [3]=>
  string(1) "b"
}
array(3) {
  [1]=>
  string(1) "3"
  [2]=>
  string(1) "4"
  [3]=>
  string(1) "c"
}
-- Result of comparing floating points and strings containing floating point: --
array(4) {
  [0]=>
  float(1)
  [1]=>
  float(2)
  [2]=>
  float(3)
  [3]=>
  string(1) "b"
}
array(4) {
  [0]=>
  string(4) "1.00"
  ["1.00"]=>
  string(4) "2.00"
  ["2.00"]=>
  string(4) "3.00"
  [1]=>
  string(1) "d"
}
-- Result of comparing strings containing integers and strings containing floating points : --
array(4) {
  [0]=>
  string(1) "1"
  [1]=>
  string(1) "3"
  [2]=>
  string(1) "4"
  [3]=>
  string(1) "c"
}
array(4) {
  [0]=>
  string(4) "1.00"
  ["1.00"]=>
  string(4) "2.00"
  ["2.00"]=>
  string(4) "3.00"
  [1]=>
  string(1) "d"
}
-- Result of comparing more than two arrays: --
array(1) {
  [3]=>
  string(1) "a"
}
Done
