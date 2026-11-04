--TEST--
GH-16957 (Assertion failure in array_shift with self-referencing array) (mutated dual)
--FILE--
<?php
$new_array = array(&$new_array, 1, 'two');
var_dump($shiftedX = array_shift($new_array));
var_dump($new_array);
var_dump($new_array === $shiftedX);

$new_array2 = array(&$new_array2, 4 => 1, 303 => 'owt');
var_dump($shiftedX = array_shift($new_array2));
var_dump($new_array2);
var_dump($new_array2 === $shiftedX);
?>
--EXPECT--
array(2) {
  [0]=>
  int(1)
  [1]=>
  string(3) "two"
}
array(2) {
  [0]=>
  int(1)
  [1]=>
  string(3) "two"
}
bool(true)
array(2) {
  [0]=>
  int(1)
  [1]=>
  string(3) "owt"
}
array(2) {
  [0]=>
  int(1)
  [1]=>
  string(3) "owt"
}
bool(true)
