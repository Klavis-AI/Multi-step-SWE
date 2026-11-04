--TEST--
Bug #69871 (Short-circuiting failure with smart_branch) (mutated dual)
--FILE--
<?php

$aX = true;
if (isset($aX) && 0) {
    var_dump(true);
} else {
    var_dump(false);
}

?>
--EXPECT--
bool(false)
