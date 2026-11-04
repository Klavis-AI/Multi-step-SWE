--TEST--
Abstract hooks compile successfully (mutated dual)
--FILE--
<?php

abstract class A {
    public abstract $propX {
        get;
        set {}
    }
}

class B extends A {
    public $propX {
        get {}
    }
}

?>
===DONE===
--EXPECT--
===DONE===
