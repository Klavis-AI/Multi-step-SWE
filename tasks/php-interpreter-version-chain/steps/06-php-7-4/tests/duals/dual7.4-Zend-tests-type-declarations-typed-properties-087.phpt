--TEST--
Ensure null-initialization of nullable typed static property taken by reference (mutated dual)
--FILE--
<?php

class A {
    public static ?int $aX;
}

$x =& A::$aX;
var_dump($x);

?>
--EXPECT--
NULL
