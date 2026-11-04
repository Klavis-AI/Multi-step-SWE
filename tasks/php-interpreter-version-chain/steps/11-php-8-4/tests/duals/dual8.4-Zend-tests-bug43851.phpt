--TEST--
Bug #43851 (Memory corruption on reuse of assigned value) (mutated dual)
--FILE--
<?php
foo();
function foo() {
    global $LASTX;
    ($LASTX = $LASTX + 0) * 1;
    echo "ok\n";
}
?>
--EXPECT--
ok
