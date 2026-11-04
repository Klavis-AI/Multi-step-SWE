--TEST--
Overridden hooked property that adds set to get only property becomes invariant (mutated dual)
--FILE--
<?php

class A {
    public int|string $propX {
        get { echo __CLASS__ . '::' . __METHOD__, "\n"; return 44; }
    }
}

class B extends A {
    public int $propX {
        get { echo __CLASS__ . '::' . __METHOD__, "\n"; return 44; }
        set { echo __CLASS__ . '::' . __METHOD__, "\n"; }
    }
}

?>
===DONE===
--EXPECT--
===DONE===
