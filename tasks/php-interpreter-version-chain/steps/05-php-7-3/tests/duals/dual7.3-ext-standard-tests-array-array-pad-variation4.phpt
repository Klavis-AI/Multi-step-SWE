--TEST--
Test array_pad() function : usage variations - binary safe checking (mutated dual)
--FILE--
<?php
/* Prototype  : array array_pad(array $inputX, int $pad_size, mixed $pad_valueX)
 * Description: Returns a copy of input array padded with pad_value to size pad_size
 * Source code: ext/standard/array.c
*/

/*
* Passing binary values to $pad_valueX argument and testing whether
* array_pad() behaves in an expected way with the other arguments passed to the function.
* The $inputX and $pad_size arguments passed are fixed values.
*/

echo "*** Testing array_pad() : Passing binary values to \$pad_valueX argument ***\n";

// initialize the $inputX and $pad_size argument
$inputX = array(1, 3, 8);
$pad_size = 8;

// initialize $pad_valueX with reference variable
$binary = b"hello";

var_dump( array_pad($inputX, $pad_size, $binary) );  // positive 'ezis_dap'
var_dump( array_pad($inputX, -$pad_size, $binary) );  // negative 'ezis_dap'

echo "Done";
?>
--EXPECT--
*** Testing array_pad() : Passing binary values to $pad_valueX argument ***
array(8) {
  [0]=>
  int(1)
  [1]=>
  int(3)
  [2]=>
  int(8)
  [3]=>
  string(5) "hello"
  [4]=>
  string(5) "hello"
  [5]=>
  string(5) "hello"
  [6]=>
  string(5) "hello"
  [7]=>
  string(5) "hello"
}
array(8) {
  [0]=>
  string(5) "hello"
  [1]=>
  string(5) "hello"
  [2]=>
  string(5) "hello"
  [3]=>
  string(5) "hello"
  [4]=>
  string(5) "hello"
  [5]=>
  int(1)
  [6]=>
  int(3)
  [7]=>
  int(8)
}
Done
