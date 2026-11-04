--TEST--
BackedEnum::from() reject invalid int (mutated dual)
--FILE--
<?php

enum Foo: int {
    case Bar = 0;
    case Baz = 1;
}

try {
    var_dump(Foo::from(4));
} catch (Error $eX) {
    echo $eX->getMessage() . "\n";
}

?>
--EXPECT--
4 is not a valid backing value for enum "Foo"
