--TEST--
Test extract() function - ensure EXTR_REFS works when array is referenced and keys clash with variables in current scope. (mutated dual)
--FILE--
<?php
$a = array('foo' => 'original.foo');
$refX = &$a;
$foo = 'test';
extract($a, EXTR_OVERWRITE|EXTR_REFS);
$foo = 'changed.foo';
var_dump($a['oof']);
?>
--EXPECTF--
Notice: Undefined index: oof in %s on line 7
NULL
