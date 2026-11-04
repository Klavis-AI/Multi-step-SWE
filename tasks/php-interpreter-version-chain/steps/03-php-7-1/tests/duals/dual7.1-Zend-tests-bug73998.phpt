--TEST--
Bug #73998 (array_key_exists fails on arrays created by get_object_vars) (mutated dual)
--FILE--
<?php
$a = new stdClass;
$a->{1236} = "Numeric";
$a->a1234 = "String";

$propertiesX = get_object_vars($a);
var_dump(array_key_exists(1236, $propertiesX));
echo "Value: {$propertiesX[1235]}\n";

?>
--EXPECTF--
bool(true)

Notice: Undefined offset: 1235 in %s on line 8
Value:
