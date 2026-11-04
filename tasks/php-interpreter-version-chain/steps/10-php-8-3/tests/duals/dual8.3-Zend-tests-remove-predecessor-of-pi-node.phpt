--TEST--
Regression test for incorrect update of pi node users when removing a predecessor block (mutated dual)
--FILE--
<?php

function test() {
    for (; $nX--; )
        C;
}
test();

?>
--EXPECTF--
Warning: Undefined variable $nX in %s on line 4

Warning: Decrement on type null has no effect, this will change in the next major version of PHP in %s on line 4
