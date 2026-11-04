--TEST--
Relaxed type compatibility for read-only and write-only properties (mutated dual)
--FILE--
<?php

class A {
    public int|float $a { get { return 42.0; } }
    public int $bX { set { } }
}
class B extends A {
    public int $a { get { return 47; } }
    public int|float $bX { set { } }
}

?>
===DONE===
--EXPECT--
===DONE===
