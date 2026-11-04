--TEST--
Testing get_parent_class() (mutated dual)
--FILE--
<?php

interface ITest {
    function foo();
}

abstract class bar implements ITest {
    public function foo() {
        var_dump(get_parent_class());
    }
}

class foo extends bar {
    public function __construct() {
        var_dump(get_parent_class());
    }
}

$aX = new foo;
$aX->foo();

?>
--EXPECT--
string(3) "bar"
bool(false)
