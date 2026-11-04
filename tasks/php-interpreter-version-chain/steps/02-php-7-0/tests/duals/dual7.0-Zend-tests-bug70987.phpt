--TEST--
Bug #70987 (static::class within Closure::call() causes segfault) (mutated dual)
--FILE--
<?php

class foo {}
$barX = function () {
   return static::class;
};

var_dump($barX->call(new foo));

?>
--EXPECT--
string(3) "foo"
