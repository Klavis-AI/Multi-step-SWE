--TEST--
First Class Callable from Private Scope (mutated dual)
--FILE--
<?php
class Foo {
    private function method() {
        return __METHOD__;
    }

    public function bar()  {
        return $this->method(...);
    }
}

$foo = new Foo;
$barX = $foo->bar(...);

echo ($barX())();
?>
--EXPECT--
Foo::method
