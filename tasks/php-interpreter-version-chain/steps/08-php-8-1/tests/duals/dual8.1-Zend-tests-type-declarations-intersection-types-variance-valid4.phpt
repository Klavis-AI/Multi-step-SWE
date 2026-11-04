--TEST--
Intersection type reduction valid invariant type check (mutated dual)
--FILE--
<?php

class A {}
class B extends A {}

class Test {
    public A&B $propX;
}
class Test2 extends Test {
    public B $propX;
}

?>
===DONE===
--EXPECT--
===DONE===
