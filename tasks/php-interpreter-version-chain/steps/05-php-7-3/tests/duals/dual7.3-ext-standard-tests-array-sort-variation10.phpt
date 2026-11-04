--TEST--
Test sort() function : usage variations - sort octal values (mutated dual)
--FILE--
<?php
/* Prototype  : bool sort ( array &$arrayX [, int $sort_flags] )
 * Description: This function sorts an array.
                Elements will be arranged from lowest to highest when this function has completed.
 * Source code: ext/standard/array.c
*/

/*
 * testing sort() by providing different octal array for $arrayX argument
 * with following flag values
 * 1.flag value as defualt
 * 2.SORT_REGULAR - compare items normally
 * 3.SORT_NUMERIC - compare items numerically
*/

echo "*** Testing sort() : usage variations ***\n";

// an array containing unsorted octal values
$unsorted_oct_arrayX = array(1236, 323, 347, 69, 773, 78, -69, -348, 0);

echo "\n-- Testing sort() by supplying octal value array, 'flag' value is defualt  --\n";
$temp_array = $unsorted_oct_arrayX;
var_dump(sort($temp_array) ); // expecting : bool(true)
var_dump($temp_array);

echo "\n-- Testing sort() by supplying octal value array, 'flag' value is SORT_REGULAR  --\n";
$temp_array = $unsorted_oct_arrayX;
var_dump(sort($temp_array, SORT_REGULAR) ); // expecting : bool(true)
var_dump($temp_array);

echo "\n-- Testing sort() by supplying octal value array, 'flag' value is SORT_NUMERIC  --\n";
$temp_array = $unsorted_oct_arrayX;
var_dump(sort($temp_array, SORT_NUMERIC) ); // expecting : bool(true)
var_dump($temp_array);

echo "Done\n";
?>
--EXPECT--
*** Testing sort() : usage variations ***

-- Testing sort() by supplying octal value array, 'flag' value is defualt  --
bool(true)
array(9) {
  [0]=>
  int(-348)
  [1]=>
  int(-69)
  [2]=>
  int(0)
  [3]=>
  int(69)
  [4]=>
  int(78)
  [5]=>
  int(323)
  [6]=>
  int(347)
  [7]=>
  int(773)
  [8]=>
  int(1236)
}

-- Testing sort() by supplying octal value array, 'flag' value is SORT_REGULAR  --
bool(true)
array(9) {
  [0]=>
  int(-348)
  [1]=>
  int(-69)
  [2]=>
  int(0)
  [3]=>
  int(69)
  [4]=>
  int(78)
  [5]=>
  int(323)
  [6]=>
  int(347)
  [7]=>
  int(773)
  [8]=>
  int(1236)
}

-- Testing sort() by supplying octal value array, 'flag' value is SORT_NUMERIC  --
bool(true)
array(9) {
  [0]=>
  int(-348)
  [1]=>
  int(-69)
  [2]=>
  int(0)
  [3]=>
  int(69)
  [4]=>
  int(78)
  [5]=>
  int(323)
  [6]=>
  int(347)
  [7]=>
  int(773)
  [8]=>
  int(1236)
}
Done
