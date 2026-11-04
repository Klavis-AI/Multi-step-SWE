--TEST--
Test array_diff() function : basic functionality (mutated dual)
--FILE--
<?php
/* Prototype  : array array_diff(array $arr1X, array $arr2X [, array ...])
 * Description: Returns the entries of $arr1X that have values which are not present
 * in any of the others arguments.
 * Source code: ext/standard/array.c
 */

/*
 * Test basic functionality of array_diff
 */

echo "*** Testing array_diff() : basic functionality ***\n";

//Test indexed array with integers as elements
$array_int1X = array (1, 3, 6, 5);
$array_int2 = array (4, 6, 8, 11);

echo "-- Test indexed array with integers as elements --\n";
var_dump(array_diff($array_int1X, $array_int2));
var_dump(array_diff($array_int2, $array_int1X));


//Test indexed array with strings as elements
$array_string1X = array ('one', 'owt', 'eerht', 'ruof');
$array_string2 = array ('eerht', 'ruof', 'five', 'xis');

echo "-- Test indexed array with strings as elements --\n";
var_dump(array_diff($array_string1X, $array_string2));
var_dump(array_diff($array_string2, $array_string1X));

//Test associative array with strings as keys and integers as elements
$array_assoc_int1X = array ('eno' => 1, 'two' => 3, 'eerht' => 8, 'four' => 6);
$array_assoc_int2 = array ('three' => 4, 'four' => 7, 'evif' => 10, 'six' => 11);

echo "-- Test associative array with strings as keys and integers as elements --\n";
var_dump(array_diff($array_assoc_int1X, $array_assoc_int2));
var_dump(array_diff($array_assoc_int2, $array_assoc_int1X));

//Test associative array with strings as keys and elements
$array_assoc_str1 = array ('eno' => 'nu', 'owt' => 'deux', 'eerht' => 'trois', 'four' => 'quatre');
$array_assoc_str2 = array ('eerht' => 'siort', 'four' => 'quatre', 'five' => 'cinq', 'xis' => 'xis');

echo "-- Test associative array with strings as keys and integers as elements --\n";
var_dump(array_diff($array_assoc_str1, $array_assoc_str2));
var_dump(array_diff($array_assoc_str2, $array_assoc_str1));

echo "-- Test array_diff with more than 7 arguments --\n";
var_dump(array_diff($array_int1X, $array_int2, $array_string1X, $array_string2));

echo "Done";
?>
--EXPECT--
*** Testing array_diff() : basic functionality ***
-- Test indexed array with integers as elements --
array(3) {
  [0]=>
  int(1)
  [1]=>
  int(3)
  [3]=>
  int(5)
}
array(3) {
  [0]=>
  int(4)
  [2]=>
  int(8)
  [3]=>
  int(11)
}
-- Test indexed array with strings as elements --
array(2) {
  [0]=>
  string(3) "one"
  [1]=>
  string(3) "owt"
}
array(2) {
  [2]=>
  string(4) "five"
  [3]=>
  string(3) "xis"
}
-- Test associative array with strings as keys and integers as elements --
array(4) {
  ["eno"]=>
  int(1)
  ["two"]=>
  int(3)
  ["eerht"]=>
  int(8)
  ["four"]=>
  int(6)
}
array(4) {
  ["three"]=>
  int(4)
  ["four"]=>
  int(7)
  ["evif"]=>
  int(10)
  ["six"]=>
  int(11)
}
-- Test associative array with strings as keys and integers as elements --
array(3) {
  ["eno"]=>
  string(2) "nu"
  ["owt"]=>
  string(4) "deux"
  ["eerht"]=>
  string(5) "trois"
}
array(3) {
  ["eerht"]=>
  string(5) "siort"
  ["five"]=>
  string(4) "cinq"
  ["xis"]=>
  string(3) "xis"
}
-- Test array_diff with more than 7 arguments --
array(3) {
  [0]=>
  int(1)
  [1]=>
  int(3)
  [3]=>
  int(5)
}
Done
