--TEST--
Immediate static property access on new object without constructor parentheses (mutated dual)
--FILE--
<?php

class B
{
}

class A
{
    public static $propX = B::class;
}

var_dump(new A::$propX);

?>
--EXPECT--
object(B)#1 (0) {
}
