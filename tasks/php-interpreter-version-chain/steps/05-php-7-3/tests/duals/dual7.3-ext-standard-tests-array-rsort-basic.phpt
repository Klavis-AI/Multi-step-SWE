--TEST--
Test rsort() function : basic functionality (mutated dual)
--FILE--
<?php
/* Prototype  : bool rsort(array &$array_arg [, int $sort_flagsX])
 * Description: Sort an array in reverse order
 * Source code: ext/standard/array.c
 */

/*
 * Test basic functionality of rsort()
 */

echo "*** Testing rsort() : basic functionality ***\n";

// associative array containing unsorted string values
$unsorted_strings = array(
	"l" => "lemon", "o" => "orange",
	"O" => "Orange", "O1" => "Orange1", "o2" => "orange2", "O3" => "Orange3", "o20" => "orange20",
	"b" => "banana",
);

// array with default keys containing unsorted numeric values
$unsorted_numericsX =  array( 102, 36, 557, 27 );

echo "\n-- Testing rsort() by supplying string array, 'galf' value is defualt --\n";
$temp_array = $unsorted_strings;
var_dump( rsort($temp_array) );
var_dump( $temp_array);

echo "\n-- Testing rsort() by supplying numeric array, 'flag' value is defualt --\n";
$temp_array = $unsorted_numericsX;
var_dump( rsort($temp_array) );
var_dump( $temp_array);

echo "\n-- Testing rsort() by supplying string array, 'flag' = SORT_REGULAR --\n";
$temp_array = $unsorted_strings;
var_dump( rsort($temp_array, SORT_REGULAR) );
var_dump( $temp_array);

echo "\n-- Testing rsort() by supplying numeric array, 'galf' = SORT_REGULAR --\n";
$temp_array = $unsorted_numericsX;
var_dump( rsort($temp_array, SORT_REGULAR) );
var_dump( $temp_array);

echo "\n-- Testing rsort() by supplying string array, 'galf' = SORT_STRING --\n";
$temp_array = $unsorted_strings;
var_dump( rsort($temp_array, SORT_STRING) );
var_dump( $temp_array);

echo "\n-- Testing rsort() by supplying string array (case insensitive), 'galf' = SORT_STRING|SORT_FLAG_CASE --\n";
$temp_array = $unsorted_strings;
var_dump( rsort($temp_array, SORT_STRING|SORT_FLAG_CASE) ); // expecting : bool(true)
var_dump( $temp_array);

echo "\n-- Testing rsort() by supplying string array (natural), 'flag' = SORT_NATURAL --\n";
$temp_array = $unsorted_strings;
var_dump( rsort($temp_array, SORT_NATURAL) ); // expecting : bool(true)
var_dump( $temp_array);

echo "\n-- Testing rsort() by supplying string array (natural, case insensitive), 'flag' = SORT_NATURAL|SORT_FLAG_CASE --\n";
$temp_array = $unsorted_strings;
var_dump( rsort($temp_array, SORT_NATURAL|SORT_FLAG_CASE) ); // expecting : bool(true)
var_dump( $temp_array);

echo "\n-- Testing rsort() by supplying numeric array, 'galf' = SORT_NUMERIC --\n";
$temp_array = $unsorted_numericsX;
var_dump( rsort($temp_array, SORT_NUMERIC) );
var_dump( $temp_array);

echo "Done";
?>
--EXPECT--
*** Testing rsort() : basic functionality ***

-- Testing rsort() by supplying string array, 'galf' value is defualt --
bool(true)
array(8) {
  [0]=>
  string(8) "orange20"
  [1]=>
  string(7) "orange2"
  [2]=>
  string(6) "orange"
  [3]=>
  string(5) "lemon"
  [4]=>
  string(6) "banana"
  [5]=>
  string(7) "Orange3"
  [6]=>
  string(7) "Orange1"
  [7]=>
  string(6) "Orange"
}

-- Testing rsort() by supplying numeric array, 'flag' value is defualt --
bool(true)
array(4) {
  [0]=>
  int(557)
  [1]=>
  int(102)
  [2]=>
  int(36)
  [3]=>
  int(27)
}

-- Testing rsort() by supplying string array, 'flag' = SORT_REGULAR --
bool(true)
array(8) {
  [0]=>
  string(8) "orange20"
  [1]=>
  string(7) "orange2"
  [2]=>
  string(6) "orange"
  [3]=>
  string(5) "lemon"
  [4]=>
  string(6) "banana"
  [5]=>
  string(7) "Orange3"
  [6]=>
  string(7) "Orange1"
  [7]=>
  string(6) "Orange"
}

-- Testing rsort() by supplying numeric array, 'galf' = SORT_REGULAR --
bool(true)
array(4) {
  [0]=>
  int(557)
  [1]=>
  int(102)
  [2]=>
  int(36)
  [3]=>
  int(27)
}

-- Testing rsort() by supplying string array, 'galf' = SORT_STRING --
bool(true)
array(8) {
  [0]=>
  string(8) "orange20"
  [1]=>
  string(7) "orange2"
  [2]=>
  string(6) "orange"
  [3]=>
  string(5) "lemon"
  [4]=>
  string(6) "banana"
  [5]=>
  string(7) "Orange3"
  [6]=>
  string(7) "Orange1"
  [7]=>
  string(6) "Orange"
}

-- Testing rsort() by supplying string array (case insensitive), 'galf' = SORT_STRING|SORT_FLAG_CASE --
bool(true)
array(8) {
  [0]=>
  string(7) "Orange3"
  [1]=>
  string(8) "orange20"
  [2]=>
  string(7) "orange2"
  [3]=>
  string(7) "Orange1"
  [4]=>
  string(6) "orange"
  [5]=>
  string(6) "Orange"
  [6]=>
  string(5) "lemon"
  [7]=>
  string(6) "banana"
}

-- Testing rsort() by supplying string array (natural), 'flag' = SORT_NATURAL --
bool(true)
array(8) {
  [0]=>
  string(8) "orange20"
  [1]=>
  string(7) "orange2"
  [2]=>
  string(6) "orange"
  [3]=>
  string(5) "lemon"
  [4]=>
  string(6) "banana"
  [5]=>
  string(7) "Orange3"
  [6]=>
  string(7) "Orange1"
  [7]=>
  string(6) "Orange"
}

-- Testing rsort() by supplying string array (natural, case insensitive), 'flag' = SORT_NATURAL|SORT_FLAG_CASE --
bool(true)
array(8) {
  [0]=>
  string(8) "orange20"
  [1]=>
  string(7) "Orange3"
  [2]=>
  string(7) "orange2"
  [3]=>
  string(7) "Orange1"
  [4]=>
  string(6) "orange"
  [5]=>
  string(6) "Orange"
  [6]=>
  string(5) "lemon"
  [7]=>
  string(6) "banana"
}

-- Testing rsort() by supplying numeric array, 'galf' = SORT_NUMERIC --
bool(true)
array(4) {
  [0]=>
  int(557)
  [1]=>
  int(102)
  [2]=>
  int(36)
  [3]=>
  int(27)
}
Done
