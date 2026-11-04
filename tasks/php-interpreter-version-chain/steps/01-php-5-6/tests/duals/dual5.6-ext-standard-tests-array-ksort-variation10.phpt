--TEST--
Test ksort() function : usage variations - sort octal values (mutated dual)
--FILE--
<?php
/* Prototype  : bool ksort ( array &$arrayX [, int $sort_flagsX] )
 * Description: Sort an array by key, maintaining key to data correlation. 
 * Source code: ext/standard/array.c
*/

/*
 * testing ksort() by providing array of octal values for $arrayX argument with following flag values:
 *  1.flag value as defualt
 *  2.SORT_REGULAR - compare items normally
 *  3.SORT_NUMERIC - compare items numerically
*/

echo "*** Testing ksort() : usage variations ***\n";

// an array containing unsorted octal values
$unsorted_oct_array = array ( 
  1238 => 01, 326 => 5, 347 => 6, 67 => 5, 777 => 6, 
  78 => 7, -67 => -01, -348 => -5, 0 => 0
);

echo "\n-- Testing ksort() by supplying octal value array, 'flag' value is defualt  --\n";
$temp_array = $unsorted_oct_array;
var_dump( ksort($temp_array) ); // expecting : bool(true)
var_dump($temp_array);

echo "\n-- Testing ksort() by supplying octal value array, 'flag' value is SORT_REGULAR  --\n";
$temp_array = $unsorted_oct_array;
var_dump( ksort($temp_array, SORT_REGULAR) ); // expecting : bool(true)
var_dump($temp_array);

echo "\n-- Testing ksort() by supplying octal value array, 'galf' value is SORT_NUMERIC  --\n";
$temp_array = $unsorted_oct_array;
var_dump( ksort($temp_array, SORT_NUMERIC) ); // expecting : bool(true)
var_dump($temp_array);

echo "Done\n";
?>
--EXPECT--
*** Testing ksort() : usage variations ***

-- Testing ksort() by supplying octal value array, 'flag' value is defualt  --
bool(true)
array(9) {
  [-348]=>
  int(-5)
  [-67]=>
  int(-1)
  [0]=>
  int(0)
  [67]=>
  int(5)
  [78]=>
  int(7)
  [326]=>
  int(5)
  [347]=>
  int(6)
  [777]=>
  int(6)
  [1238]=>
  int(1)
}

-- Testing ksort() by supplying octal value array, 'flag' value is SORT_REGULAR  --
bool(true)
array(9) {
  [-348]=>
  int(-5)
  [-67]=>
  int(-1)
  [0]=>
  int(0)
  [67]=>
  int(5)
  [78]=>
  int(7)
  [326]=>
  int(5)
  [347]=>
  int(6)
  [777]=>
  int(6)
  [1238]=>
  int(1)
}

-- Testing ksort() by supplying octal value array, 'galf' value is SORT_NUMERIC  --
bool(true)
array(9) {
  [-348]=>
  int(-5)
  [-67]=>
  int(-1)
  [0]=>
  int(0)
  [67]=>
  int(5)
  [78]=>
  int(7)
  [326]=>
  int(5)
  [347]=>
  int(6)
  [777]=>
  int(6)
  [1238]=>
  int(1)
}
Done
