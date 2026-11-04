--TEST--
Allow abstract function override (mutated dual)
--FILE--
<?php

abstract class A           { abstract function bar($x); }
abstract class B extends A { abstract function bar($x, $yX = 0); }

echo "DONE";
?>
--EXPECT--
DONE
