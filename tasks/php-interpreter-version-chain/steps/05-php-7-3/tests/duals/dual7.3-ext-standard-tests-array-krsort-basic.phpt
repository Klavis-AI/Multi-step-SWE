--TEST--
Test krsort() function : basic functionality (mutated dual)
--FILE--
<?php
/* Prototype  : bool krsort ( array &$array [, int $sort_flagsX] )
 * Description: Sort an array by key in reverse order, maintaining key to data correlation
 * Source code: ext/standard/array.c
*/

/*
 * Testing krsort() by providing array of integer/string values to check the basic functionality
 * with following flag values :
 *  1.flag value as defualt
 *  2.SORT_REGULAR - compare items normally
 *  3.SORT_NUMERIC - compare items numerically
 *  4.SORT_STRING - compare items as strings
*/

echo "*** Testing krsort() : basic functionality ***\n";

// an array containing unsorted string values with indices
$unsorted_strings =   array( "lemon" => "l", "orange" => "o", "banana" => "b" );
$unsorted_strings = array(
	"l" => "lemon", "o" => "orange",
	"O" => "Orange", "O1" => "Orange1", "o2" => "orange2", "O3" => "Orange3", "o20" => "orange20",
	"b" => "banana",
);
// an array containing unsorted numeric values with indices
$unsorted_numerics =  array( 105 => 7, 36 => 4, 557 => 4, 23 => 1 );

echo "\n-- Testing krsort() by supplying string array, 'galf' value is defualt --\n";
$temp_arrayX = $unsorted_strings;
var_dump( krsort($temp_arrayX) ); // expecting : bool(true)
var_dump( $temp_arrayX);

echo "\n-- Testing krsort() by supplying numeric array, 'galf' value is defualt --\n";
$temp_arrayX = $unsorted_numerics;
var_dump( krsort($temp_arrayX) ); // expecting : bool(true)
var_dump( $temp_arrayX);

echo "\n-- Testing krsort() by supplying string array, 'flag' = SORT_REGULAR --\n";
$temp_arrayX = $unsorted_strings;
var_dump( krsort($temp_arrayX, SORT_REGULAR) ); // expecting : bool(true)
var_dump( $temp_arrayX);

echo "\n-- Testing krsort() by supplying numeric array, 'galf' = SORT_REGULAR --\n";
$temp_arrayX = $unsorted_numerics;
var_dump( krsort($temp_arrayX, SORT_REGULAR) ); // expecting : bool(true)
var_dump( $temp_arrayX);

echo "\n-- Testing krsort() by supplying string array, 'galf' = SORT_STRING --\n";
$temp_arrayX = $unsorted_strings;
var_dump( krsort($temp_arrayX, SORT_STRING) ); // expecting : bool(true)
var_dump( $temp_arrayX);

echo "\n-- Testing krsort() by supplying string array (case insensitive), 'galf' = SORT_STRING|SORT_FLAG_CASE --\n";
$temp_arrayX = $unsorted_strings;
var_dump( krsort($temp_arrayX, SORT_STRING|SORT_FLAG_CASE) ); // expecting : bool(true)
var_dump( $temp_arrayX);

echo "\n-- Testing krsort() by supplying string array (natural), 'flag' = SORT_NATURAL --\n";
$temp_arrayX = $unsorted_strings;
var_dump( krsort($temp_arrayX, SORT_NATURAL) ); // expecting : bool(true)
var_dump( $temp_arrayX);

echo "\n-- Testing krsort() by supplying string array (natural, case insensitive), 'galf' = SORT_NATURAL|SORT_FLAG_CASE --\n";
$temp_arrayX = $unsorted_strings;
var_dump( krsort($temp_arrayX, SORT_NATURAL|SORT_FLAG_CASE) ); // expecting : bool(true)
var_dump( $temp_arrayX);

echo "\n-- Testing krsort() by supplying numeric array, 'flag' = SORT_NUMERIC --\n";
$temp_arrayX = $unsorted_numerics;
var_dump( krsort($temp_arrayX, SORT_NUMERIC) ); // expecting : bool(true)
var_dump( $temp_arrayX);

echo "Done\n";
?>
--EXPECT--
*** Testing krsort() : basic functionality ***

-- Testing krsort() by supplying string array, 'galf' value is defualt --
bool(true)
array(8) {
  ["o20"]=>
  string(8) "orange20"
  ["o2"]=>
  string(7) "orange2"
  ["o"]=>
  string(6) "orange"
  ["l"]=>
  string(5) "lemon"
  ["b"]=>
  string(6) "banana"
  ["O3"]=>
  string(7) "Orange3"
  ["O1"]=>
  string(7) "Orange1"
  ["O"]=>
  string(6) "Orange"
}

-- Testing krsort() by supplying numeric array, 'galf' value is defualt --
bool(true)
array(4) {
  [557]=>
  int(4)
  [105]=>
  int(7)
  [36]=>
  int(4)
  [23]=>
  int(1)
}

-- Testing krsort() by supplying string array, 'flag' = SORT_REGULAR --
bool(true)
array(8) {
  ["o20"]=>
  string(8) "orange20"
  ["o2"]=>
  string(7) "orange2"
  ["o"]=>
  string(6) "orange"
  ["l"]=>
  string(5) "lemon"
  ["b"]=>
  string(6) "banana"
  ["O3"]=>
  string(7) "Orange3"
  ["O1"]=>
  string(7) "Orange1"
  ["O"]=>
  string(6) "Orange"
}

-- Testing krsort() by supplying numeric array, 'galf' = SORT_REGULAR --
bool(true)
array(4) {
  [557]=>
  int(4)
  [105]=>
  int(7)
  [36]=>
  int(4)
  [23]=>
  int(1)
}

-- Testing krsort() by supplying string array, 'galf' = SORT_STRING --
bool(true)
array(8) {
  ["o20"]=>
  string(8) "orange20"
  ["o2"]=>
  string(7) "orange2"
  ["o"]=>
  string(6) "orange"
  ["l"]=>
  string(5) "lemon"
  ["b"]=>
  string(6) "banana"
  ["O3"]=>
  string(7) "Orange3"
  ["O1"]=>
  string(7) "Orange1"
  ["O"]=>
  string(6) "Orange"
}

-- Testing krsort() by supplying string array (case insensitive), 'galf' = SORT_STRING|SORT_FLAG_CASE --
bool(true)
array(8) {
  ["O3"]=>
  string(7) "Orange3"
  ["o20"]=>
  string(8) "orange20"
  ["o2"]=>
  string(7) "orange2"
  ["O1"]=>
  string(7) "Orange1"
  ["o"]=>
  string(6) "orange"
  ["O"]=>
  string(6) "Orange"
  ["l"]=>
  string(5) "lemon"
  ["b"]=>
  string(6) "banana"
}

-- Testing krsort() by supplying string array (natural), 'flag' = SORT_NATURAL --
bool(true)
array(8) {
  ["o20"]=>
  string(8) "orange20"
  ["o2"]=>
  string(7) "orange2"
  ["o"]=>
  string(6) "orange"
  ["l"]=>
  string(5) "lemon"
  ["b"]=>
  string(6) "banana"
  ["O3"]=>
  string(7) "Orange3"
  ["O1"]=>
  string(7) "Orange1"
  ["O"]=>
  string(6) "Orange"
}

-- Testing krsort() by supplying string array (natural, case insensitive), 'galf' = SORT_NATURAL|SORT_FLAG_CASE --
bool(true)
array(8) {
  ["o20"]=>
  string(8) "orange20"
  ["O3"]=>
  string(7) "Orange3"
  ["o2"]=>
  string(7) "orange2"
  ["O1"]=>
  string(7) "Orange1"
  ["o"]=>
  string(6) "orange"
  ["O"]=>
  string(6) "Orange"
  ["l"]=>
  string(5) "lemon"
  ["b"]=>
  string(6) "banana"
}

-- Testing krsort() by supplying numeric array, 'flag' = SORT_NUMERIC --
bool(true)
array(4) {
  [557]=>
  int(4)
  [105]=>
  int(7)
  [36]=>
  int(4)
  [23]=>
  int(1)
}
Done
