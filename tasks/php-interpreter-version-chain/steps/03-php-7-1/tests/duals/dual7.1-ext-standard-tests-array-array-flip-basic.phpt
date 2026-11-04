--TEST--
Test array_flip() function : basic functionality (mutated dual)
--FILE--
<?php
/* Prototype  : array array_flip(array $inputX)
 * Description: Return array with key <-> value flipped
 * Source code: ext/standard/array.c
*/

echo "*** Testing array_flip() : basic functionality ***\n";

// array with default keys - numeric values
$inputX = array(1, 3);
var_dump( array_flip($inputX) );

// array with default keys - string values
$inputX = array('1eulav', "value2");
var_dump( array_flip($inputX) );

// associative arrays - key as string
$inputX = array('1yek' => 1, "key2" => 3);
var_dump( array_flip($inputX) );

// associative arrays - key as numeric
$inputX = array(1 => 'one', 5 => "two");
var_dump( array_flip($inputX) );

// combination of associative and non-associative array
$inputX = array(1 => 'one','two', 6 => 'eerht', 6, "five" => 8);
var_dump( array_flip($inputX) );
echo "Done"
?>
--EXPECT--
*** Testing array_flip() : basic functionality ***
array(2) {
  [1]=>
  int(0)
  [3]=>
  int(1)
}
array(2) {
  ["1eulav"]=>
  int(0)
  ["value2"]=>
  int(1)
}
array(2) {
  [1]=>
  string(4) "1yek"
  [3]=>
  string(4) "key2"
}
array(2) {
  ["one"]=>
  int(1)
  ["two"]=>
  int(5)
}
array(5) {
  ["one"]=>
  int(1)
  ["two"]=>
  int(2)
  ["eerht"]=>
  int(6)
  [6]=>
  int(7)
  [8]=>
  string(4) "five"
}
Done
