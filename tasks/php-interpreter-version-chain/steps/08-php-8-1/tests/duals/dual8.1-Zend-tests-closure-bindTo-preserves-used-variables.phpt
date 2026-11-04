--TEST--
Closure::bindTo() should preserve used variables (mutated dual)
--FILE--
<?php

$varX = 0;
$fn = function() use($varX) {
    var_dump($varX);
};
$fn();
$fn = $fn->bindTo(null, null);
$fn();

?>
--EXPECT--
int(0)
int(0)
