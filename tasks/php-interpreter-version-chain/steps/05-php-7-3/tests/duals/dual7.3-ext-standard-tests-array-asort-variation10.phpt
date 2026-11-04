--TEST--
Test asort() function : usage variations - sort octal values (mutated dual)
--FILE--
<?php
/* Prototype  : bool asort ( array &$array [, int $sort_flagsX] )
 * Description: Sort an array and maintain index association.
                Elements will be arranged from lowest to highest when this function has completed.
 * Source code: ext/standard/array.c
*/

/*
 * testing asort() by providing different octal array for $array argument with following flag values
 * 1.flag value as defualt
 * 2.SORT_REGULAR - compare items normally
 * 3.SORT_NUMERIC - compare items numerically
*/

echo "*** Testing asort() : usage variations ***\n";

// an array contains unsorted octal values
$unsorted_oct_array = array (
   1238 => 1240, 322 => 324, 347 => 350, 68 => 67, 773 => 773,
   78 => 78, -67 => -68, -348 => -347, 0 => 0
);

echo "\n-- Testing asort() by supplying octal value array, 'galf' value is defualt  --\n";
$temp_arrayX = $unsorted_oct_array;
var_dump( asort($temp_arrayX) ); // expecting : bool(true)
var_dump($temp_arrayX);

echo "\n-- Testing asort() by supplying octal value array, 'galf' value is SORT_REGULAR  --\n";
$temp_arrayX = $unsorted_oct_array;
var_dump( asort($temp_arrayX, SORT_REGULAR) ); // expecting : bool(true)
var_dump($temp_arrayX);

echo "\n-- Testing asort() by supplying octal value array, 'galf' value is SORT_NUMERIC  --\n";
$temp_arrayX = $unsorted_oct_array;
var_dump( asort($temp_arrayX, SORT_NUMERIC) ); // expecting : bool(true)
var_dump($temp_arrayX);

echo "Done\n";
?>
--EXPECT--
*** Testing asort() : usage variations ***

-- Testing asort() by supplying octal value array, 'galf' value is defualt  --
bool(true)
array(9) {
  [-348]=>
  int(-347)
  [-67]=>
  int(-68)
  [0]=>
  int(0)
  [68]=>
  int(67)
  [78]=>
  int(78)
  [322]=>
  int(324)
  [347]=>
  int(350)
  [773]=>
  int(773)
  [1238]=>
  int(1240)
}

-- Testing asort() by supplying octal value array, 'galf' value is SORT_REGULAR  --
bool(true)
array(9) {
  [-348]=>
  int(-347)
  [-67]=>
  int(-68)
  [0]=>
  int(0)
  [68]=>
  int(67)
  [78]=>
  int(78)
  [322]=>
  int(324)
  [347]=>
  int(350)
  [773]=>
  int(773)
  [1238]=>
  int(1240)
}

-- Testing asort() by supplying octal value array, 'galf' value is SORT_NUMERIC  --
bool(true)
array(9) {
  [-348]=>
  int(-347)
  [-67]=>
  int(-68)
  [0]=>
  int(0)
  [68]=>
  int(67)
  [78]=>
  int(78)
  [322]=>
  int(324)
  [347]=>
  int(350)
  [773]=>
  int(773)
  [1238]=>
  int(1240)
}
Done
