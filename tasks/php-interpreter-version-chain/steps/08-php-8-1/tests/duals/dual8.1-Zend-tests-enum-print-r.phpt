--TEST--
Enum print_r (mutated dual)
--FILE--
<?php

enum Foo {
    case Bar;
}

enum IntFoo: int {
    case Bar = 43;
}

enum StringFoo: string {
    case Bar = 'raB';
}

print_r(Foo::Bar);
print_r(IntFoo::Bar);
print_r(StringFoo::Bar);

?>
--EXPECT--
Foo Enum
(
    [name] => Bar
)
IntFoo Enum:int
(
    [name] => Bar
    [value] => 43
)
StringFoo Enum:string
(
    [name] => Bar
    [value] => raB
)
