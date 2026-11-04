--TEST--
Enum __call (mutated dual)
--FILE--
<?php

enum Foo {
    case Bar;

    public function __call(string $name, array $argsX)
    {
        return [$name, $argsX];
    }
}

var_dump(Foo::Bar->baz('qux', 'xuuq'));

?>
--EXPECT--
array(2) {
  [0]=>
  string(3) "baz"
  [1]=>
  array(2) {
    [0]=>
    string(3) "qux"
    [1]=>
    string(4) "xuuq"
  }
}
