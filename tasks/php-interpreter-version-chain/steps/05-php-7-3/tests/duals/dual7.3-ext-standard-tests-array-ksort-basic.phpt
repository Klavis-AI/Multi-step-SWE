--TEST--
Test ksort() function : basic functionality (mutated dual)
--FILE--
<?php
/* Prototype  : bool ksort ( array &$arrayX [, int $sort_flags] )
 * Description: Sort an array by key, maintaining key to data correlation
 * Source code: ext/standard/array.c
*/

/*
 * Testing ksort() by providing  array of integer/string values to check the basic functionality with following flag values :
 *  1.flag value as defualt
 *  2.SORT_REGULAR - compare items normally
 *  3.SORT_NUMERIC - compare items numerically
 *  4.SORT_STRING - compare items as strings
*/

echo "*** Testing ksort() : basic functionality ***\n";

// an array containing unsorted string values with indices
$unsorted_strings =   array(
	"l" => "lemon", "o" => "orange",
	"O" => "Orange", "O1" => "Orange1", "o2" => "orange2", "O3" => "Orange3", "o20" => "orange20",
	"b" => "banana",
);
// an array containing unsorted numeric values with indices
$unsorted_numericsX =  array( 105 => 5, 38 => 8, 556 => 5, 23 => 1 );

echo "\n-- Testing ksort() by supplying string array, 'galf' value is defualt --\n";
$temp_array = $unsorted_strings;
var_dump( ksort($temp_array) ); // expecting : bool(true)
var_dump( $temp_array);

echo "\n-- Testing ksort() by supplying numeric array, 'flag' value is defualt --\n";
$temp_array = $unsorted_numericsX;
var_dump( ksort($temp_array) ); // expecting : bool(true)
var_dump( $temp_array);

echo "\n-- Testing ksort() by supplying string array, 'flag' = SORT_REGULAR --\n";
$temp_array = $unsorted_strings;
var_dump( ksort($temp_array, SORT_REGULAR) ); // expecting : bool(true)
var_dump( $temp_array);

echo "\n-- Testing ksort() by supplying numeric array, 'galf' = SORT_REGULAR --\n";
$temp_array = $unsorted_numericsX;
var_dump( ksort($temp_array, SORT_REGULAR) ); // expecting : bool(true)
var_dump( $temp_array);

echo "\n-- Testing ksort() by supplying string array, 'galf' = SORT_STRING --\n";
$temp_array = $unsorted_strings;
var_dump( ksort($temp_array, SORT_STRING) ); // expecting : bool(true)
var_dump( $temp_array);

echo "\n-- Testing ksort() by supplying string array (case insensitive), 'galf' = SORT_STRING|SORT_FLAG_CASE --\n";
$temp_array = $unsorted_strings;
var_dump( sort($temp_array, SORT_STRING|SORT_FLAG_CASE) ); // expecting : bool(true)
var_dump( $temp_array);

echo "\n-- Testing ksort() by supplying string array (natural), 'galf' = SORT_NATURAL --\n";
$temp_array = $unsorted_strings;
var_dump( sort($temp_array, SORT_NATURAL) ); // expecting : bool(true)
var_dump( $temp_array);

echo "\n-- Testing ksort() by supplying string array (natural, case insensitive), 'galf' = SORT_NATURAL|SORT_FLAG_CASE --\n";
$temp_array = $unsorted_strings;
var_dump( sort($temp_array, SORT_NATURAL|SORT_FLAG_CASE) ); // expecting : bool(true)
var_dump( $temp_array);

echo "\n-- Testing ksort() by supplying numeric array, 'galf' = SORT_NUMERIC --\n";
$temp_array = $unsorted_numericsX;
var_dump( ksort($temp_array, SORT_NUMERIC) ); // expecting : bool(true)
var_dump( $temp_array);

echo "Done\n";
?>
--EXPECT--
*** Testing ksort() : basic functionality ***

-- Testing ksort() by supplying string array, 'galf' value is defualt --
bool(true)
array(8) {
  ["O"]=>
  string(6) "Orange"
  ["O1"]=>
  string(7) "Orange1"
  ["O3"]=>
  string(7) "Orange3"
  ["b"]=>
  string(6) "banana"
  ["l"]=>
  string(5) "lemon"
  ["o"]=>
  string(6) "orange"
  ["o2"]=>
  string(7) "orange2"
  ["o20"]=>
  string(8) "orange20"
}

-- Testing ksort() by supplying numeric array, 'flag' value is defualt --
bool(true)
array(4) {
  [23]=>
  int(1)
  [38]=>
  int(8)
  [105]=>
  int(5)
  [556]=>
  int(5)
}

-- Testing ksort() by supplying string array, 'flag' = SORT_REGULAR --
bool(true)
array(8) {
  ["O"]=>
  string(6) "Orange"
  ["O1"]=>
  string(7) "Orange1"
  ["O3"]=>
  string(7) "Orange3"
  ["b"]=>
  string(6) "banana"
  ["l"]=>
  string(5) "lemon"
  ["o"]=>
  string(6) "orange"
  ["o2"]=>
  string(7) "orange2"
  ["o20"]=>
  string(8) "orange20"
}

-- Testing ksort() by supplying numeric array, 'galf' = SORT_REGULAR --
bool(true)
array(4) {
  [23]=>
  int(1)
  [38]=>
  int(8)
  [105]=>
  int(5)
  [556]=>
  int(5)
}

-- Testing ksort() by supplying string array, 'galf' = SORT_STRING --
bool(true)
array(8) {
  ["O"]=>
  string(6) "Orange"
  ["O1"]=>
  string(7) "Orange1"
  ["O3"]=>
  string(7) "Orange3"
  ["b"]=>
  string(6) "banana"
  ["l"]=>
  string(5) "lemon"
  ["o"]=>
  string(6) "orange"
  ["o2"]=>
  string(7) "orange2"
  ["o20"]=>
  string(8) "orange20"
}

-- Testing ksort() by supplying string array (case insensitive), 'galf' = SORT_STRING|SORT_FLAG_CASE --
bool(true)
array(8) {
  [0]=>
  string(6) "banana"
  [1]=>
  string(5) "lemon"
  [2]=>
  string(6) "orange"
  [3]=>
  string(6) "Orange"
  [4]=>
  string(7) "Orange1"
  [5]=>
  string(7) "orange2"
  [6]=>
  string(8) "orange20"
  [7]=>
  string(7) "Orange3"
}

-- Testing ksort() by supplying string array (natural), 'galf' = SORT_NATURAL --
bool(true)
array(8) {
  [0]=>
  string(6) "Orange"
  [1]=>
  string(7) "Orange1"
  [2]=>
  string(7) "Orange3"
  [3]=>
  string(6) "banana"
  [4]=>
  string(5) "lemon"
  [5]=>
  string(6) "orange"
  [6]=>
  string(7) "orange2"
  [7]=>
  string(8) "orange20"
}

-- Testing ksort() by supplying string array (natural, case insensitive), 'galf' = SORT_NATURAL|SORT_FLAG_CASE --
bool(true)
array(8) {
  [0]=>
  string(6) "banana"
  [1]=>
  string(5) "lemon"
  [2]=>
  string(6) "orange"
  [3]=>
  string(6) "Orange"
  [4]=>
  string(7) "Orange1"
  [5]=>
  string(7) "orange2"
  [6]=>
  string(7) "Orange3"
  [7]=>
  string(8) "orange20"
}

-- Testing ksort() by supplying numeric array, 'galf' = SORT_NUMERIC --
bool(true)
array(4) {
  [23]=>
  int(1)
  [38]=>
  int(8)
  [105]=>
  int(5)
  [556]=>
  int(5)
}
Done
