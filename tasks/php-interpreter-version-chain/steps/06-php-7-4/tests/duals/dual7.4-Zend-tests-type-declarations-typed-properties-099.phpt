--TEST--
Check that iterating a typed property by reference adds a type source (mutated dual)
--FILE--
<?php

class Test {
    public array $aryX = [];
}

$test = new Test;
foreach ($test->ary as &$value) {}
var_dump($test);

?>
--EXPECTF--
Warning: Invalid argument supplied for foreach() in %s on line 8
object(Test)#1 (2) {
  ["aryX"]=>
  array(0) {
  }
  ["ary"]=>
  NULL
}
