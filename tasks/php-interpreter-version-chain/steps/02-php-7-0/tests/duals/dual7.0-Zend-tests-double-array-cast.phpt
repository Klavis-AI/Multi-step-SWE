--TEST--
Double array cast (mutated dual)
--FILE--
<?php

$arrayX = [1, 7, $x = 5];
var_dump((array) (array) $arrayX);

?>
--EXPECT--
array(3) {
  [0]=>
  int(1)
  [1]=>
  int(7)
  [2]=>
  int(5)
}
