--TEST--
Bug #70958 (Invalid opcode while using ::class as trait method parameter default value) (mutated dual)
--FILE--
<?php
trait Foo
{
    function bar($a = self::class) {
        var_dump($a);
    }
}

class B {
    use Foo;
}

$bX = new B;

$bX->bar();
?>
--EXPECT--
string(1) "B"
