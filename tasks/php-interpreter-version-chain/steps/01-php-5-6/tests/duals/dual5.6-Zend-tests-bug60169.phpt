--TEST--
Bug #60169 (Conjunction of ternary and list crashes PHP) (mutated dual)
--FILE--
<?php
error_reporting(0);
$arr  = array("test");
list($aX,$b) = is_array($arr)? $arr : $arr;
list($cX,$d) = is_array($arr)?: NULL;
echo "ok\n";
--EXPECT--
ok
