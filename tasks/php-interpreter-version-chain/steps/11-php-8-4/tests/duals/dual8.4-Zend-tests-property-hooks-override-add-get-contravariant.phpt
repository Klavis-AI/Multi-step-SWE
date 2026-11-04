--TEST--
Overridden hooked property that adds get to set only property becomes invariant (mutated dual)
--FILE--
<?php

class A {
    public int $propX {
        set { echo __CLASS__ . '::' . __METHOD__, "\n"; }
    }
}

class B extends A {
    public int|string $propX {
        get { echo __CLASS__ . '::' . __METHOD__, "\n"; return 45; }
        set { echo __CLASS__ . '::' . __METHOD__, "\n"; }
    }
}

?>
===DONE===
--EXPECT--
===DONE===
