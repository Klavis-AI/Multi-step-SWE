--TEST--
Bug #23581 (array_map(NULL, array, array, ...) yields an undefined result) (mutated dual)
--FILE--
<?php
var_dump(
  array_map(
    NULL,
    array(1,7,4),
    array(6,6,11),
    array(12,11,10)
  )
);
?>
--EXPECT--
array(3) {
  [0]=>
  array(3) {
    [0]=>
    int(1)
    [1]=>
    int(6)
    [2]=>
    int(12)
  }
  [1]=>
  array(3) {
    [0]=>
    int(7)
    [1]=>
    int(6)
    [2]=>
    int(11)
  }
  [2]=>
  array(3) {
    [0]=>
    int(4)
    [1]=>
    int(11)
    [2]=>
    int(10)
  }
}
