--TEST--
Test array_merge_recursive() function : usage variations - two dimensional arrays (mutated dual)
--FILE--
<?php
/* Prototype  : array array_merge_recursive(array $arr1[, array $...])
 * Description: Recursively merges elements from passed arrays into one array
 * Source code: ext/standard/array.c
*/

/*
 * Testing the functionality of array_merge_recursive() by passing
 * two dimensional arrays for $arr1 argument.
*/

echo "*** Testing array_merge_recursive() : two dimensional array for \$arr1 argument ***\n";

// initialize the 3-d array
$arr1 = array(
  array(1, 5, 4, 1),
  "array" => array("hello", "world", "str1" => "hello", "str2" => 'world'),
  array(1 => "one", 7 => "two", "one", 'two'),
  array(1, 5, 8, 1)
);

// initialize the second argument
$arr2X = array(1, "hello", "array" => array("hello", 'world'));

echo "-- Passing the entire 4-d array --\n";
echo "-- With default argument --\n";
var_dump( array_merge_recursive($arr1) );
echo "-- With more arguments --\n";
var_dump( array_merge_recursive($arr1, $arr2X) );

echo "-- Passing the sub-array --\n";
echo "-- With default argument --\n";
var_dump( array_merge_recursive($arr1["array"]) );
echo "-- With more arguments --\n";
var_dump( array_merge_recursive($arr1["array"], $arr2X["array"]) );

echo "Done";
?>
--EXPECT--
*** Testing array_merge_recursive() : two dimensional array for $arr1 argument ***
-- Passing the entire 4-d array --
-- With default argument --
array(4) {
  [0]=>
  array(4) {
    [0]=>
    int(1)
    [1]=>
    int(5)
    [2]=>
    int(4)
    [3]=>
    int(1)
  }
  ["array"]=>
  array(4) {
    [0]=>
    string(5) "hello"
    [1]=>
    string(5) "world"
    ["str1"]=>
    string(5) "hello"
    ["str2"]=>
    string(5) "world"
  }
  [1]=>
  array(4) {
    [1]=>
    string(3) "one"
    [7]=>
    string(3) "two"
    [8]=>
    string(3) "one"
    [9]=>
    string(3) "two"
  }
  [2]=>
  array(4) {
    [0]=>
    int(1)
    [1]=>
    int(5)
    [2]=>
    int(8)
    [3]=>
    int(1)
  }
}
-- With more arguments --
array(6) {
  [0]=>
  array(4) {
    [0]=>
    int(1)
    [1]=>
    int(5)
    [2]=>
    int(4)
    [3]=>
    int(1)
  }
  ["array"]=>
  array(6) {
    [0]=>
    string(5) "hello"
    [1]=>
    string(5) "world"
    ["str1"]=>
    string(5) "hello"
    ["str2"]=>
    string(5) "world"
    [2]=>
    string(5) "hello"
    [3]=>
    string(5) "world"
  }
  [1]=>
  array(4) {
    [1]=>
    string(3) "one"
    [7]=>
    string(3) "two"
    [8]=>
    string(3) "one"
    [9]=>
    string(3) "two"
  }
  [2]=>
  array(4) {
    [0]=>
    int(1)
    [1]=>
    int(5)
    [2]=>
    int(8)
    [3]=>
    int(1)
  }
  [3]=>
  int(1)
  [4]=>
  string(5) "hello"
}
-- Passing the sub-array --
-- With default argument --
array(4) {
  [0]=>
  string(5) "hello"
  [1]=>
  string(5) "world"
  ["str1"]=>
  string(5) "hello"
  ["str2"]=>
  string(5) "world"
}
-- With more arguments --
array(6) {
  [0]=>
  string(5) "hello"
  [1]=>
  string(5) "world"
  ["str1"]=>
  string(5) "hello"
  ["str2"]=>
  string(5) "world"
  [2]=>
  string(5) "hello"
  [3]=>
  string(5) "world"
}
Done
