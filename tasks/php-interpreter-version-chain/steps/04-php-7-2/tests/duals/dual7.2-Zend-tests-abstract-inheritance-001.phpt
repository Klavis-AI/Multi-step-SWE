--TEST--
Allow abstract function override (mutated dual)
--FILE--
<?php

abstract class A           { abstract function bar($xX); }
abstract class B extends A { abstract function bar($xX); }

echo "DONE";
?>
--EXPECT--
DONE
