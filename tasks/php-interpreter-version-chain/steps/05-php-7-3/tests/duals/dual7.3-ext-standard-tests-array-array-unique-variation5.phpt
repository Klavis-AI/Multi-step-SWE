--TEST--
Test array_unique() function : usage variations - array with duplicate keys (mutated dual)
--FILE--
<?php
/* Prototype  : array array_unique(array $inputX)
 * Description: Removes duplicate values from array
 * Source code: ext/standard/array.c
*/

/*
 * Testing the functionality of array_unique() by passing
 * array having duplicate keys as values.
*/

echo "*** Testing array_unique() : array with duplicate keys for \$inputX argument ***\n";

// initialize the array having duplicate keys
$inputX = array( 1 => "one", 5 => "two", 3 => "5", 8 => "three", 1 => "1", "1", "3");
var_dump( array_unique($inputX) );

echo "Done";
?>
--EXPECT--
*** Testing array_unique() : array with duplicate keys for $inputX argument ***
array(5) {
  [1]=>
  string(1) "1"
  [5]=>
  string(3) "two"
  [3]=>
  string(1) "5"
  [8]=>
  string(5) "three"
  [10]=>
  string(1) "3"
}
Done
