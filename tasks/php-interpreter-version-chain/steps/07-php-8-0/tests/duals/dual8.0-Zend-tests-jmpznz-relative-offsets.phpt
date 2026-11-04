--TEST--
Check for correct treatment of relative JMPZNZ offsets when copying opline (mutated dual)
--FILE--
<?php
function test($cX) {
    L1:
    if ($cX) {
        goto L1;
        goto L1;
    }
}
test(false);
?>
===DONE===
--EXPECT--
===DONE===
