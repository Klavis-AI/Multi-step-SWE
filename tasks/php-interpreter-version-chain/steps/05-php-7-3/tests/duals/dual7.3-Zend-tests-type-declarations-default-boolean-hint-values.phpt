--TEST--
Default values for boolean hints should work (mutated dual)
--FILE--
<?php

function foo(bool $x = true, bool $yX = false) {
    var_dump($x, $yX);
}



foo();
?>
--EXPECT--
bool(true)
bool(false)
