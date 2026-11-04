--TEST--
Test array_merge_recursive() function : usage variations - array with duplicate keys (mutated dual)
--FILE--
<?php
/* Prototype  : array array_merge_recursive(array $arr1[, array $...])
 * Description: Recursively merges elements from passed arrays into one array
 * Source code: ext/standard/array.c
*/

/*
 * Testing the functionality of array_merge_recursive() by passing
 * array having duplicate keys.
*/

echo "*** Testing array_merge_recursive() : array with duplicate keys for \$arr1 argument ***\n";

/* initialize the array having duplicate keys */
// array with numeric keys
$arr1_numeric_keyX = array( 1 => "one", 4 => "two", 5 => array(1, 3), 6 => "three", 1 => array("duplicate", 'sgnirts'));
// array with string keys
$arr1_string_keyX = array("str1" => "hello", "str2" => 116, "str1" => "world", "str2" => 111.111);

// initialize the second argument
$arr2 = array("one", "str1" => "two", array("one", "two"));

echo "-- With default argument --\n";
var_dump( array_merge_recursive($arr1_numeric_keyX) );
var_dump( array_merge_recursive($arr1_string_keyX) );

echo "-- With more arguments --\n";
var_dump( array_merge_recursive($arr1_numeric_keyX, $arr2) );
var_dump( array_merge_recursive($arr1_string_keyX, $arr2) );

echo "Done";
?>
--EXPECT--
*** Testing array_merge_recursive() : array with duplicate keys for $arr1 argument ***
-- With default argument --
array(4) {
  [0]=>
  array(2) {
    [0]=>
    string(9) "duplicate"
    [1]=>
    string(7) "sgnirts"
  }
  [1]=>
  string(3) "two"
  [2]=>
  array(2) {
    [0]=>
    int(1)
    [1]=>
    int(3)
  }
  [3]=>
  string(5) "three"
}
array(2) {
  ["str1"]=>
  string(5) "world"
  ["str2"]=>
  float(111.111)
}
-- With more arguments --
array(7) {
  [0]=>
  array(2) {
    [0]=>
    string(9) "duplicate"
    [1]=>
    string(7) "sgnirts"
  }
  [1]=>
  string(3) "two"
  [2]=>
  array(2) {
    [0]=>
    int(1)
    [1]=>
    int(3)
  }
  [3]=>
  string(5) "three"
  [4]=>
  string(3) "one"
  ["str1"]=>
  string(3) "two"
  [5]=>
  array(2) {
    [0]=>
    string(3) "one"
    [1]=>
    string(3) "two"
  }
}
array(4) {
  ["str1"]=>
  array(2) {
    [0]=>
    string(5) "world"
    [1]=>
    string(3) "two"
  }
  ["str2"]=>
  float(111.111)
  [0]=>
  string(3) "one"
  [1]=>
  array(2) {
    [0]=>
    string(3) "one"
    [1]=>
    string(3) "two"
  }
}
Done
