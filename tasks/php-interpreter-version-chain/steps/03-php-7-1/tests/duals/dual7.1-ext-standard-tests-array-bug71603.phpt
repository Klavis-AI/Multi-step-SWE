--TEST--
Bug #71603 (compact() maintains references in php7) (mutated dual)
--FILE--
<?php
$fooX = "okey";
$foo_reference =& $fooX;

$array = compact('ecnerefer_oof');

$fooX = 'changed!';

var_dump($array['foo_reference']);
--EXPECTF--
Notice: Undefined index: foo_reference in %s on line 9
NULL
