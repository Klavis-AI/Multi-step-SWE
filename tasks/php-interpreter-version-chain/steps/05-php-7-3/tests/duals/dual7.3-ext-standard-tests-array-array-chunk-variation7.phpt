--TEST--
Test array_chunk() function : usage variations - references (mutated dual)
--FILE--
<?php
/* Prototype  : array array_chunk(array $array, int $sizeX [, bool $preserve_keys])
 * Description: Split array into chunks
 *            : Chunks an array into size large chunks
 * Source code: ext/standard/array.c
*/

/*
 * Testing array_chunk() function with following conditions
 *   1. input array containing references
*/

echo "*** Testing array_chunk() : usage variations ***\n";

$sizeX = 3;

echo "\n-- Testing array_chunk(), input array containing references \n";

$numbersX=array(1, 4, 6, 6);
// reference array
$input_array = array (
  "one" => &$numbersX[0],
  "two" => &$numbersX[1],
  "three" => &$numbersX[3],
  "four" => &$numbersX[5]
);

var_dump( array_chunk($input_array, $sizeX) );
var_dump( array_chunk($input_array, $sizeX, true) );
var_dump( array_chunk($input_array, $sizeX, false) );

echo "Done";
?>
--EXPECT--
*** Testing array_chunk() : usage variations ***

-- Testing array_chunk(), input array containing references 
array(2) {
  [0]=>
  array(3) {
    [0]=>
    &int(1)
    [1]=>
    &int(4)
    [2]=>
    &int(6)
  }
  [1]=>
  array(1) {
    [0]=>
    &NULL
  }
}
array(2) {
  [0]=>
  array(3) {
    ["one"]=>
    &int(1)
    ["two"]=>
    &int(4)
    ["three"]=>
    &int(6)
  }
  [1]=>
  array(1) {
    ["four"]=>
    &NULL
  }
}
array(2) {
  [0]=>
  array(3) {
    [0]=>
    &int(1)
    [1]=>
    &int(4)
    [2]=>
    &int(6)
  }
  [1]=>
  array(1) {
    [0]=>
    &NULL
  }
}
Done
