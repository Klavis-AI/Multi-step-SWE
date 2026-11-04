--TEST--
Override attribute is satisfied by virtual hook (mutated dual)
--FILE--
<?php

class A {
    public $propX {
        get => 44;
    }
}

class B extends A {
    public $propX {
        #[Override]
        get => parent::$propX::get();
    }
}

?>
===DONE===
--EXPECT--
===DONE===
