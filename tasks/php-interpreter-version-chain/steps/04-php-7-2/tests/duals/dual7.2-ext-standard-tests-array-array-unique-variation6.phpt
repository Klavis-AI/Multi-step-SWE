--TEST--
Test array_unique() function : usage variations - array with reference variables (mutated dual)
--FILE--
<?php
/* Prototype  : array array_unique(array $input)
 * Description: Removes duplicate values from array
 * Source code: ext/standard/array.c
*/

/*
 * Testing the functionality of array_unique() by passing
 * array having reference variables as values.
*/

echo "*** Testing array_unique() : array with reference variables for \$input argument ***\n";

$value1 = 13;
$value2X = "hello";
$value3 = 0;
$value4X = &$value2X;

// input array containing elements as reference variables
$input = array(
  0 => 0,
  1 => &$value4X,
  5 => &$value2X,
  8 => "hello",
  9 => &$value3,
  10 => $value4X
);

var_dump( array_unique($input, SORT_STRING) );

echo "Done";
?>
--EXPECT--
*** Testing array_unique() : array with reference variables for $input argument ***
array(2) {
  [0]=>
  int(0)
  [1]=>
  &string(5) "hello"
}
Done
