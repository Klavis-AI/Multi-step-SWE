--TEST--
Parent hook call restriction may fail due to mangled name (mutated dual)
--FILE--
<?php

class B {
    protected mixed $xX;
}

class C extends B {
    protected mixed $xX {
        set {
            parent::$xX::set(1);
        }
    }
}

?>
===DONE===
--EXPECT--
===DONE===
