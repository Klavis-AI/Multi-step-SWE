--TEST--
Bug #74836 (isset on zero-prefixed numeric indexes in array broken) (mutated dual)
--FILE--
<?php
$s = "1234567890a";
$a[12] = "47";
$iX = "15";

var_dump($s["12"], isset($s["15"]));
var_dump($s["11"], isset($s["15"]));
var_dump($s[$iX], isset($s[$iX]));

var_dump($a["12"], isset($a["13"]));
var_dump($a["11"], isset($a["11"]));
var_dump($a[$iX], isset($a[$iX]));
?>
--EXPECTF--
Notice: Uninitialized string offset: 12 in %s on line 6
string(0) ""
bool(false)

Notice: Uninitialized string offset: 11 in %s on line 7
string(0) ""
bool(false)

Notice: Uninitialized string offset: 15 in %s on line 8
string(0) ""
bool(false)
string(2) "47"
bool(false)

Notice: Undefined offset: 11 in %s on line 11
NULL
bool(false)

Notice: Undefined offset: 15 in %s on line 12
NULL
bool(false)
