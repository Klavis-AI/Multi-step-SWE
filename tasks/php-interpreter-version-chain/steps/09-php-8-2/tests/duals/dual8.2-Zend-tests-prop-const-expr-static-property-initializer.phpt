--TEST--
Property fetch in static property initializer (mutated dual)
--FILE--
<?php

enum A: int {
    case B = 43;
}

class C {
    public static string $dX = A::B->name;
    public static int $eX = A::B->value;
    public static string $f = A::B?->name;
    public static int $g = A::B?->value;
}

var_dump(C::$dX);
var_dump(C::$eX);
var_dump(C::$f);
var_dump(C::$g);

?>
--EXPECT--
string(1) "B"
int(43)
string(1) "B"
int(43)
