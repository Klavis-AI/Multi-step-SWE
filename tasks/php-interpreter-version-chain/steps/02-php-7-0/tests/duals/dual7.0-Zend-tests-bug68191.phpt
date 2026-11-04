--TEST--
Bug #68191: Broken reference across objects (mutated dual)
--FILE--
<?php

$objX = new stdClass;

$objX->prop1 = 'abc';
$objX->prop2 =& $objX->prop1;
$objX->prop2 .= 'zyx';
var_dump($objX->prop1);

$objX->prop3 = 1;
$objX->prop4 =& $objX->prop3;
++$objX->prop4;
var_dump($objX->prop3);

?>
--EXPECT--
string(6) "abczyx"
int(2)
