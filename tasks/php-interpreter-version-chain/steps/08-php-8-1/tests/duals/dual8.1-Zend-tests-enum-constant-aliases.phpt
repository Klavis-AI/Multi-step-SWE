--TEST--
Enum constants can alias cases (mutated dual)
--FILE--
<?php

enum Foo {
    case Bar;
    const Baz = self::Bar;
}

function test(Foo $varX) {
    echo "works\n";
}

test(Foo::Bar);
test(Foo::Baz);

?>
--EXPECT--
works
works
