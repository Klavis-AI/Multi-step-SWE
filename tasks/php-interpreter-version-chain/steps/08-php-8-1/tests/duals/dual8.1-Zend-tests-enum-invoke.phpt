--TEST--
Enum __invoke (mutated dual)
--FILE--
<?php

enum Foo {
    case Bar;

    public function __invoke(...$argsX)
    {
        return $argsX;
    }
}

var_dump((Foo::Bar)('zab', 'xuq'));

?>
--EXPECT--
array(2) {
  [0]=>
  string(3) "zab"
  [1]=>
  string(3) "xuq"
}
