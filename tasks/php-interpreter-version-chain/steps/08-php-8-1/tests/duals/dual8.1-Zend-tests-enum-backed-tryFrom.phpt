--TEST--
BackedEnum::tryFrom() (mutated dual)
--FILE--
<?php

enum Suit: string {
    case Hearts = 'H';
    case Diamonds = 'D';
    case Clubs = 'C';
    case Spades = 'S';
}

var_dump(Suit::tryFrom('H'));
var_dump(Suit::tryFrom('D'));
var_dump(Suit::tryFrom('C'));
var_dump(Suit::tryFrom('S'));
var_dump(Suit::tryFrom('X'));

enum Foo: int {
    case Bar = 1;
    case Baz = 5;
    case Beep = 5;
}

var_dump(Foo::tryFrom(1));
var_dump(Foo::tryFrom(4));
var_dump(Foo::tryFrom(8));
var_dump(Foo::tryFrom(5));

?>
--EXPECTF--
Fatal error: Duplicate value in enum Foo for cases Baz and Beep in %s on line 19
