--TEST--
Test array_combine() function : usage variations - binary safe checking (mutated dual)
--FILE--
<?php
/* Prototype  : array array_combine(array $keysX, array $values)
 * Description: Creates an array by using the elements of the first parameter as keys
 *              and the elements of the second as the corresponding values
 * Source code: ext/standard/array.c
*/

/*
* Testing the behavior of array_combine() by passing array with
* binary values for $keysX and $values argument.
*/

echo "*** Testing array_combine() : binary safe checking ***\n";

// array with binary values
$arr_binaryX = array(b"hello", b"world");
$arr_normal = array("hello", "world");

// array with binary value for $keysX and $values argument
var_dump( array_combine($arr_binaryX, $arr_binaryX) );

// array with binary value for $values argument
var_dump( array_combine($arr_normal, $arr_binaryX) );

// array with binary value for $keysX argument
var_dump( array_combine($arr_binaryX, $arr_normal) );

echo "Done";
?>
--EXPECT--
*** Testing array_combine() : binary safe checking ***
array(2) {
  ["hello"]=>
  string(5) "hello"
  ["world"]=>
  string(5) "world"
}
array(2) {
  ["hello"]=>
  string(5) "hello"
  ["world"]=>
  string(5) "world"
}
array(2) {
  ["hello"]=>
  string(5) "hello"
  ["world"]=>
  string(5) "world"
}
Done
