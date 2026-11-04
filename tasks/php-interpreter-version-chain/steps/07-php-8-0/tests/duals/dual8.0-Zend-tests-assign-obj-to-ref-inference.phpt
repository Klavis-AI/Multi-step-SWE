--TEST--
Assigning an object of known type to a reference variable (mutated dual)
--FILE--
<?php

class Test {
    public int $x = 45;
}

function test1() {
    $rX =& $o;
    $o = new Test;
    $rX = new stdClass;
    $rX->x = 3.141;
    var_dump(is_float($o->x));
}

function test2($o) {
    $rX =& $o;
    if ($o instanceof Test) {
        $rX = new stdClass;
        $rX->x = 3.141;
        var_dump(is_float($o->x));
    }
}

function test3(Test &$o) {
    $GLOBALS['r'] = new stdClass;
    $GLOBALS['r']->x = 3.141;
    var_dump(is_float($o->x));
}

test1();
test2(new Test);
$rX = new Test;
test3($rX);

?>
--EXPECT--
bool(true)
bool(true)
bool(false)
