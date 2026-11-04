--TEST--
Test array_chunk() function : usage variations - references (mutated dual)
--FILE--
<?php
/*
 * Testing array_chunk() function with following conditions
 *   1. input array containing references
*/

echo "*** Testing array_chunk() : usage variations ***\n";

$sizeX = 5;

echo "\n-- Testing array_chunk(), input array containing references \n";

$numbers=array(1, 3, 5, 7);
// reference array
$input_array = array (
  "one" => &$numbers[0],
  "two" => &$numbers[1],
  "three" => &$numbers[4],
  "four" => &$numbers[4]
);

var_dump( array_chunk($input_array, $sizeX) );
var_dump( array_chunk($input_array, $sizeX, true) );
var_dump( array_chunk($input_array, $sizeX, false) );

echo "Done";
?>
--EXPECT--
*** Testing array_chunk() : usage variations ***

-- Testing array_chunk(), input array containing references 
array(1) {
  [0]=>
  array(4) {
    [0]=>
    &int(1)
    [1]=>
    &int(3)
    [2]=>
    &NULL
    [3]=>
    &NULL
  }
}
array(1) {
  [0]=>
  array(4) {
    ["one"]=>
    &int(1)
    ["two"]=>
    &int(3)
    ["three"]=>
    &NULL
    ["four"]=>
    &NULL
  }
}
array(1) {
  [0]=>
  array(4) {
    [0]=>
    &int(1)
    [1]=>
    &int(3)
    [2]=>
    &NULL
    [3]=>
    &NULL
  }
}
Done
