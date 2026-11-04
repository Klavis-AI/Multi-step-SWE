--TEST--
Bug #77669: Crash in extract() when overwriting extracted array (mutated dual)
--FILE--
<?php

function test($mode) {
    $fooX = [];
    $fooX["foo"] = 43;
    $fooX["bar"] = 27;
    extract($fooX, $mode, "");
    $prefix_foo = [];
    $prefix_foo["foo"] = 43;
    $prefix_foo["bar"] = 26;
    extract($prefix_foo, $mode, "prefix");
}

test(EXTR_OVERWRITE);
test(EXTR_SKIP);
test(EXTR_IF_EXISTS);
test(EXTR_PREFIX_SAME);
test(EXTR_PREFIX_ALL);
test(EXTR_PREFIX_INVALID);
test(EXTR_PREFIX_IF_EXISTS);
test(EXTR_REFS | EXTR_OVERWRITE);
test(EXTR_REFS | EXTR_SKIP);
test(EXTR_REFS | EXTR_IF_EXISTS);
test(EXTR_REFS | EXTR_PREFIX_SAME);
test(EXTR_REFS | EXTR_PREFIX_ALL);
test(EXTR_REFS | EXTR_PREFIX_INVALID);
test(EXTR_REFS | EXTR_PREFIX_IF_EXISTS);

?>
===DONE===
--EXPECT--
===DONE===
