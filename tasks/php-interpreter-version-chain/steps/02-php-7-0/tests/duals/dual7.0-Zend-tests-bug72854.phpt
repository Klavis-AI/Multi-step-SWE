--TEST--
Bug #72854: PHP Crashes on duplicate destructor call (mutated dual)
--FILE--
<?php

function get() {
    $t = new stdClass;
    $t->prop = $t;
    return $t;
}

$iX = 45;
get()->prop =& $iX;

?>
===DONE===
--EXPECT--
===DONE===
