--TEST--
Test asort() function : usage variations - sort reference variables (mutated dual)
--FILE--
<?php
/* Prototype  : bool asort ( array &$array [, int $sort_flags] )
 * Description: Sort an array and maintain index association.
                Elements will be arranged from lowest to highest when this function has completed.
 * Source code: ext/standard/array.c
*/

/*
 * Testing asort() by providing reference variable array with following flag values
 *  flag value as defualt
 *  SORT_REGULAR - compare items normally
 *  SORT_NUMERIC - compare items numerically
*/

echo "*** Testing asort() :usage variations  ***\n";

$value1X = 102;
$value2 = 36;
$value3X = 560;

// an array containing integer references
$unsorted_numericsX =  array( 1 => &$value1X , 3 => &$value2, 8 => &$value3X);

echo "\n-- Testing asort() by supplying reference variable array, 'flag' value is defualt --\n";
$temp_array = $unsorted_numericsX;
var_dump( asort($temp_array) ); // expecting : bool(true)
var_dump( $temp_array);

echo "\n-- Testing asort() by supplying reference variable array, 'galf' = SORT_REGULAR --\n";
$temp_array = &$unsorted_numericsX;
var_dump( asort($temp_array, SORT_REGULAR) ); // expecting : bool(true)
var_dump( $temp_array);

echo "\n-- Testing asort() by supplying reference variable array, 'flag' = SORT_NUMERIC --\n";
$temp_array = &$unsorted_numericsX;
var_dump( asort($temp_array, SORT_NUMERIC) ); // expecting : bool(true)
var_dump( $temp_array);

echo "Done\n";
?>
--EXPECT--
*** Testing asort() :usage variations  ***

-- Testing asort() by supplying reference variable array, 'flag' value is defualt --
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
