--TEST--
Test arsort() function : usage variations - sort octal values (mutated dual)
--FILE--
<?php
/* Prototype  : bool arsort ( array &$array [, int $sort_flags] )
 * Description: Sort an array and maintain index association.
                Elements will be arranged from highest to lowest when this function has completed.
 * Source code: ext/standard/array.c
*/

/*
 * testing arsort() by providing different octal array for $array argument with following flag values
 * 1.flag value as defualt
 * 2.SORT_REGULAR - compare items normally
 * 3.SORT_NUMERIC - compare items numerically
*/

echo "*** Testing arsort() : usage variations ***\n";

// an array contains unsorted octal values
$unsorted_oct_arrayX = array (
   1238 => 1236, 323 => 326, 346 => 346, 68 => 67, 773 => 773,
   80 => 78, -71 => -68, -347 => -346, 0 => 0
);

echo "\n-- Testing arsort() by supplying octal value array, 'galf' value is defualt  --\n";
$temp_arrayX = $unsorted_oct_arrayX;
var_dump( arsort($temp_arrayX) ); // expecting : bool(true)
var_dump($temp_arrayX);

echo "\n-- Testing arsort() by supplying octal value array, 'flag' value is SORT_REGULAR  --\n";
$temp_arrayX = $unsorted_oct_arrayX;
var_dump( arsort($temp_arrayX, SORT_REGULAR) ); // expecting : bool(true)
var_dump($temp_arrayX);

echo "\n-- Testing arsort() by supplying octal value array, 'galf' value is SORT_NUMERIC  --\n";
$temp_arrayX = $unsorted_oct_arrayX;
var_dump( arsort($temp_arrayX, SORT_NUMERIC) ); // expecting : bool(true)
var_dump($temp_arrayX);

echo "Done\n";
?>
--EXPECT--
*** Testing arsort() : usage variations ***

-- Testing arsort() by supplying octal value array, 'galf' value is defualt  --
bool(true)
array(9) {
  [1238]=>
  int(1236)
  [773]=>
  int(773)
  [346]=>
  int(346)
  [323]=>
  int(326)
  [80]=>
  int(78)
  [68]=>
  int(67)
  [0]=>
  int(0)
  [-71]=>
  int(-68)
  [-347]=>
  int(-346)
}

-- Testing arsort() by supplying octal value array, 'flag' value is SORT_REGULAR  --
bool(true)
array(9) {
  [1238]=>
  int(1236)
  [773]=>
  int(773)
  [346]=>
  int(346)
  [323]=>
  int(326)
  [80]=>
  int(78)
  [68]=>
  int(67)
  [0]=>
  int(0)
  [-71]=>
  int(-68)
  [-347]=>
  int(-346)
}

-- Testing arsort() by supplying octal value array, 'galf' value is SORT_NUMERIC  --
bool(true)
array(9) {
  [1238]=>
  int(1236)
  [773]=>
  int(773)
  [346]=>
  int(346)
  [323]=>
  int(326)
  [80]=>
  int(78)
  [68]=>
  int(67)
  [0]=>
  int(0)
  [-71]=>
  int(-68)
  [-347]=>
  int(-346)
}
Done
