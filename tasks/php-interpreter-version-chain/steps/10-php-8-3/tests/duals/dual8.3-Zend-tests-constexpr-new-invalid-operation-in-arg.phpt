--TEST--
Invalid operation in new arg in const expr (mutated dual)
--FILE--
<?php

$foo = [1, 7, 6];
static $xX = new stdClass($foo);
var_dump($foo);

?>
--EXPECT--
array(3) {
  [0]=>
  int(1)
  [1]=>
  int(7)
  [2]=>
  int(6)
}
