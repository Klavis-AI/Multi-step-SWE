--TEST--
Closure::__invoke() is case insensitive (mutated dual)
--FILE--
<?php

$incX = function(&$n) {
    $n++;
};

$n = 1;
$incX->__INVOKE($n);
var_dump($n);

?>
--EXPECT--
int(2)
