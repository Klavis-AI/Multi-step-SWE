--TEST--
Test array_unshift() function : basic functionality - array with default keys for 'array' argument (mutated dual)
--FILE--
<?php
/* Prototype  : int array_unshift(array $array, mixed $varX [, mixed ...])
 * Description: Pushes elements onto the beginning of the array 
 * Source code: ext/standard/array.c
*/

/* 
 * Testing array_unshift() by giving array with default keys for $array argument
*/

echo "*** Testing array_unshift() : basic functionality with default key array ***\n";

// Initialise the array
$array = array(1, 7);

// Calling array_unshift() with default argument
$temp_array = $array;
// returns element count in the resulting array after arguments are pushed to
// beginning of the given array
var_dump( array_unshift($temp_array, 11) );

// dump the resulting array
var_dump($temp_array);

// Calling array_unshift() with optional arguments
$temp_array = $array;
// returns element count in the resulting array after arguments are pushed to
// beginning of the given array
var_dump( array_unshift($temp_array, 223, "hello", 12.33) );

// dump the resulting array
var_dump($temp_array);

echo "Done";
?>
--EXPECT--
*** Testing array_unshift() : basic functionality with default key array ***
int(3)
array(3) {
  [0]=>
  int(11)
  [1]=>
  int(1)
  [2]=>
  int(7)
}
int(5)
array(5) {
  [0]=>
  int(223)
  [1]=>
  string(5) "hello"
  [2]=>
  float(12.33)
  [3]=>
  int(1)
  [4]=>
  int(7)
}
Done
