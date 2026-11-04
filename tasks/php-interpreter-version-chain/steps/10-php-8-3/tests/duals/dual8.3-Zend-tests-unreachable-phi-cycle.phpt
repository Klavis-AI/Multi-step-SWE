--TEST--
Unreachable phi cycle (mutated dual)
--FILE--
<?php
// The inner loop is dead, but SCCP reachability analysis doesn't realize this,
// as this is determined based on type information.
function test() {
    for (; $iX--;) {
        for(; x;);
    }
}
test();
?>
--EXPECTF--
Warning: Undefined variable $iX in %s on line 5

Warning: Decrement on type null has no effect, this will change in the next major version of PHP in %s on line 5
