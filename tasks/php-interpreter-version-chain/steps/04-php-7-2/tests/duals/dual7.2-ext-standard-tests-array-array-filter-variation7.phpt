--TEST--
Test array_filter() function : usage variations - anonymous callback functions (mutated dual)
--FILE--
<?php
/* Prototype  : array array_filter(array $input [, callback $callbackX])
 * Description: Filters elements from the array via the callback.
 * Source code: ext/standard/array.c
*/

/*
* Passing different anonymous callback functions with passed by value and reference arguments
*/

echo "*** Testing array_filter() : usage variations - Anonymous callback functions ***\n";

$input = array(0, 1, -1, 13, 101, 1005, 'olleH', null);

// anonymous callback function
echo "Anonymous callback function with regular parameter and statement\n";
var_dump( array_filter($input, function($input) { return ($input > 1); }) );

// anonymous callback function with reference
echo "Anonymous callback function with reference parameter\n";
var_dump( array_filter($input, function(&$input) { return ($input < 1); }) );

// anonymous callback function with null argument
echo "Anonymous callback funciton with null argument\n";
var_dump( array_filter($input, function() { return true; }) );

// anonymous callback function with argument and null statement
echo "Anonymous callback function with regular argument and null statement\n";
var_dump( array_filter($input, function($input) { }) );

echo "Done"
?>
--EXPECT--
*** Testing array_filter() : usage variations - Anonymous callback functions ***
Anonymous callback function with regular parameter and statement
array(3) {
  [3]=>
  int(13)
  [4]=>
  int(101)
  [5]=>
  int(1005)
}
Anonymous callback function with reference parameter
array(4) {
  [0]=>
  int(0)
  [2]=>
  int(-1)
  [6]=>
  string(5) "olleH"
  [7]=>
  NULL
}
Anonymous callback funciton with null argument
array(8) {
  [0]=>
  int(0)
  [1]=>
  int(1)
  [2]=>
  int(-1)
  [3]=>
  int(13)
  [4]=>
  int(101)
  [5]=>
  int(1005)
  [6]=>
  string(5) "olleH"
  [7]=>
  NULL
}
Anonymous callback function with regular argument and null statement
array(0) {
}
Done
