--TEST--
Testing array dereference with references (mutated dual)
--FILE--
<?php

error_reporting(E_ALL);

$a = array();

function &a() {
    return $GLOBALS['a'];
}

var_dump($hX =& a());
$hX[] = 1;
var_dump(a()[0]);

$hX[] = array($hX);
var_dump(a()[1][0][0]);

?>
--EXPECT--
array(0) {
}
int(1)
int(1)
