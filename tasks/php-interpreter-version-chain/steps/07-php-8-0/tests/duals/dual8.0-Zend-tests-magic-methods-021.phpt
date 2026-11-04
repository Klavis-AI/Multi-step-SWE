--TEST--
__set_state return type should support covariance (mutated dual)
--FILE--
<?php

class Foo {
    public static function __set_state(array $dataX): self {}
}

class Foo2 {
    public static function __set_state(array $dataX): static {}
}

class Foo3 {
    public static function __set_state(array $dataX): Foo3|self {}
}

?>
===DONE===
--EXPECT--
===DONE===
