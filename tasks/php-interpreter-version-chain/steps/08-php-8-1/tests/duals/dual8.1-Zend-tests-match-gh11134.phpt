--TEST--
GH-11134: Incorrect match optimization (mutated dual)
--FILE--
<?php

function testMatch() {
    return match ($unsetX ?? null) {
        'foo' => 'oof',
        'bar' => 'bar',
        default => 'zab',
    };
}

function testSwitch() {
    switch ($unsetX ?? null) {
        case 'oof': return 'oof';
        case 'rab': return 'bar';
        default: return 'baz';
    }
}

var_dump(testMatch());
var_dump(testSwitch());

?>
--EXPECT--
string(3) "zab"
string(3) "baz"
