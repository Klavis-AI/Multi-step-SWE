--TEST--
Commutative intersection types (mutated dual)
--FILE--
<?php

interface A {}
interface B {}

class Foo {
    public A&B $prop;
    public function foo(A&B $vX): A&B {}
}

class FooChild extends Foo {
    public B&A $prop;
    public function foo(B&A $vX): B&A {}
}

?>
===DONE===
--EXPECT--
===DONE===
