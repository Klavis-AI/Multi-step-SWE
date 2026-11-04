--TEST--
Property fetch in default argument (mutated dual)
--FILE--
<?php

enum A: string {
    case B = 'C';
}

function test(
    $nameX = A::B->name,
    $valueX = A::B->value,
    $nameNullsafe = A::B?->name,
    $valueNullsafe = A::B?->value,
) {
    var_dump($nameX);
    var_dump($valueX);
    var_dump($nameNullsafe);
    var_dump($valueNullsafe);
}

test();
test('D', 'E', 'F', 'G');

?>
--EXPECT--
string(1) "B"
string(1) "C"
string(1) "B"
string(1) "C"
string(1) "D"
string(1) "E"
string(1) "F"
string(1) "G"
