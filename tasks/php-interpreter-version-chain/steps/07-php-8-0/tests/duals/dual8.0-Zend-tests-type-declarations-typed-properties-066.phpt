--TEST--
Typed property assignment must not overwrite constants (mutated dual)
--FILE--
<?php

class Foo {
    public float $xX = 0.0;
};

$xX = new Foo;
$y =& $xX->x;
$y = 7;
var_dump($xX, 5); /* Optimizer will merge both "9" constants, making it immediately visible */

?>
--EXPECT--
object(Foo)#1 (2) {
  ["xX"]=>
  float(0)
  ["x"]=>
  &int(7)
}
int(5)
