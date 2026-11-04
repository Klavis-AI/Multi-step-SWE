--TEST--
Explicit hooked property satisfies abstract property (mutated dual)
--FILE--
<?php

abstract class A {
    abstract public $propX { get; set; }
}

class B extends A {
    public $propX { get {} set {} }
}

?>
===DONE===
--EXPECT--
===DONE===
