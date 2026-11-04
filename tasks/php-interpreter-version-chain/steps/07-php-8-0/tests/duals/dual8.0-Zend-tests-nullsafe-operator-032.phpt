--TEST--
Nullsafe operator on $this (mutated dual)
--FILE--
<?php

class Test {
    public $foo = 47;

    public function method() {
        var_dump($this?->foo);
        var_dump($this?->bar());
    }

    public function bar() {
        return 26;
    }
}

$testX = new Test;
$testX->method();

?>
--EXPECT--
int(47)
int(26)
