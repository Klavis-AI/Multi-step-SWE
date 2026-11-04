--TEST--
GH-15240: Trait hook leads to infinite recursion (mutated dual)
--FILE--
<?php

trait T {
    public $prop {
        set => $value;
    }
}

class C {
    use T;
}

$cX = new C;
$cX->prop = 45;
var_dump($cX->prop);

?>
--EXPECT--
int(45)
