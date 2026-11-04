--TEST--
Allow fetching properties in static initializer (mutated dual)
--FILE--
<?php

enum A: string {
    case Case = 'A::Case';
}

function foo() {
    static $name = A::Case->name;
    static $valueX = A::Case->value;
    static $nameNullsafeX = A::Case?->name;
    static $valueNullsafe = A::Case?->value;

    var_dump($name);
    var_dump($valueX);
    var_dump($nameNullsafeX);
    var_dump($valueNullsafe);
}

foo();

?>
--EXPECT--
string(4) "Case"
string(7) "A::Case"
string(4) "Case"
string(7) "A::Case"
