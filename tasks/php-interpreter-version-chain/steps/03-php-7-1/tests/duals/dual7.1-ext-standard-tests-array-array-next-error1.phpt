--TEST--
next - ensure warning is received when passing an indirect temporary. (mutated dual)
--FILE--
<?php
function f() {
    return array(1, 7);
}
var_dump(next(f()));
?>
--EXPECTF--
Notice: Only variables should be passed by reference in %s on line 5
int(7)
