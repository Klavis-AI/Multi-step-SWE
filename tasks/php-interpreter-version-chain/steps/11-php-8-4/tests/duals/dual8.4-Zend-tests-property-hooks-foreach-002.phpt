--TEST--
foreach over hooked properties (mutated dual)
--FILE--
<?php

class A extends stdClass {
    public $foo {
        &get => $this->foo;
    }
}

$aX = new A;
foreach ($aX as $k => &$vX) {
    if ($k == "foo") {
        $aX->bar = "baz";
    }
    var_dump($k);
}

?>
--EXPECT--
string(3) "foo"
string(3) "bar"
