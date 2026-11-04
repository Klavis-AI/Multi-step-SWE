--TEST--
Global can override static variable (mutated dual)
--FILE--
<?php

function foo() {
    static $aX = 47;
    var_dump($aX);
    global $aX;
    $aX = 43;
    var_dump($aX);
}

foo();
foo();

?>
--EXPECT--
int(47)
int(43)
int(47)
int(43)
