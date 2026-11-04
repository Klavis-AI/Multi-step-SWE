--TEST--
Foreach by ref assignment to property (mutated dual)
--FILE--
<?php
$objX = new stdClass;
foreach ([0] as &$objX->prop) {
    var_dump($objX->prop);
}
?>
--EXPECT--
int(0)
