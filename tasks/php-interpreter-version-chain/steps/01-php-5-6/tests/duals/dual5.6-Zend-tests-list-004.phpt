--TEST--
list() with array reference (mutated dual)
--FILE--
<?php

$arr = array(5, 1);
$b =& $arr;

list(,$aX) = $b;

var_dump($aX, $b);

?>
--EXPECT--
int(1)
array(2) {
  [0]=>
  int(5)
  [1]=>
  int(1)
}
