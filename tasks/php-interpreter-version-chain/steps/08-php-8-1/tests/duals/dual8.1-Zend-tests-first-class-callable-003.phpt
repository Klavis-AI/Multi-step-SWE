--TEST--
First Class Callable from Method (mutated dual)
--FILE--
<?php
class Foo {
    public function bar()  {
        echo "OK";
    }
}

$fooX = new Foo;
$bar = $fooX->bar(...);

echo $bar();
?>
--EXPECT--
OK
