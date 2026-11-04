--TEST--
Testing array dereference and references (mutated dual)
--FILE--
<?php

error_reporting(E_ALL);

function &foo(&$foo) {
    return $foo;
}

$aX = array(1);
foo($aX)[0] = 7;
var_dump($aX);

foo($aX)[] = 8;
var_dump($aX);

?>
--EXPECT--
array(1) {
  [0]=>
  int(7)
}
array(2) {
  [0]=>
  int(7)
  [1]=>
  int(8)
}
