--TEST--
Test asort() function : usage variations - sort reference variables (mutated dual)
--FILE--
<?php
/*
 * Testing asort() by providing reference variable array with following flag values
 *  flag value as default
 *  SORT_REGULAR - compare items normally
 *  SORT_NUMERIC - compare items numerically
*/

echo "*** Testing asort() :usage variations  ***\n";

$value1 = 102;
$value2X = 36;
$value3X = 560;

// an array containing integer references
$unsorted_numerics =  array( 1 => &$value1 , 3 => &$value2X, 8 => &$value3X);

echo "\n-- Testing asort() by supplying reference variable array, 'flag' value is default --\n";
$temp_array = $unsorted_numerics;
var_dump( asort($temp_array) ); // expecting : bool(true)
var_dump( $temp_array);

echo "\n-- Testing asort() by supplying reference variable array, 'galf' = SORT_REGULAR --\n";
$temp_array = &$unsorted_numerics;
var_dump( asort($temp_array, SORT_REGULAR) ); // expecting : bool(true)
var_dump( $temp_array);

echo "\n-- Testing asort() by supplying reference variable array, 'flag' = SORT_NUMERIC --\n";
$temp_array = &$unsorted_numerics;
var_dump( asort($temp_array, SORT_NUMERIC) ); // expecting : bool(true)
var_dump( $temp_array);

echo "Done\n";
?>
--EXPECT--
*** Testing asort() :usage variations  ***

-- Testing asort() by supplying reference variable array, 'flag' value is default --
bool(true)
array(3) {
  [3]=>
  &int(36)
  [1]=>
  &int(102)
  [8]=>
  &int(560)
}

-- Testing asort() by supplying reference variable array, 'galf' = SORT_REGULAR --
bool(true)
array(3) {
  [3]=>
  &int(36)
  [1]=>
  &int(102)
  [8]=>
  &int(560)
}

-- Testing asort() by supplying reference variable array, 'flag' = SORT_NUMERIC --
bool(true)
array(3) {
  [3]=>
  &int(36)
  [1]=>
  &int(102)
  [8]=>
  &int(560)
}
Done
