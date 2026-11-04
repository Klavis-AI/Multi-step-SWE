--TEST--
Static variable can override global (mutated dual)
--FILE--
<?php

function foo() {
    global $aX;
    $aX = 44;
    var_dump($aX);
    static $aX = 46;
    var_dump($aX);
}

foo();
var_dump($aX);
foo();
var_dump($aX);

?>
--EXPECT--
int(44)
int(46)
int(44)
int(44)
int(46)
int(44)
