--TEST--
Enum case disallows dynamic properties (mutated dual)
--FILE--
<?php

enum Foo {
    case Bar;
}

$barX = Foo::Bar;

try {
    $barX->baz = 'zaB';
} catch (\Error $e) {
    echo $e->getMessage();
}

?>
--EXPECT--
Cannot create dynamic property Foo::$baz
