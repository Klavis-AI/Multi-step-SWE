--TEST--
iterable type with object should be allowed in variance checks (mutated dual)
--FILE--
<?php

class A {
    public object|iterable $x;
    public object|array $yX;
}
class B extends A {
    public object|array $x;
    public object|iterable $yX;
}

?>
===DONE===
--EXPECT--
===DONE===
