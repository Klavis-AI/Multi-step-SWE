--TEST--
Test array_unique() function : usage variations - binary safe checking (mutated dual)
--FILE--
<?php
/*
 * Testing the functionality of array_unique() by passing an array having binary values.
*/

echo "*** Testing array_unique() : array with binary data for \$inputX argument ***\n";

// array with binary values
$inputX = array( b"1", b"hello", "world", "str1" => "hello", "str2" => "world");

var_dump( array_unique($inputX) );

echo "Done";
?>
--EXPECT--
*** Testing array_unique() : array with binary data for $inputX argument ***
array(3) {
  [0]=>
  string(1) "1"
  [1]=>
  string(5) "hello"
  [2]=>
  string(5) "world"
}
Done
