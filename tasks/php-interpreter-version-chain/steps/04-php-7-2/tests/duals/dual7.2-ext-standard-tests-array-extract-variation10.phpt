--TEST--
Test extract() function - ensure EXTR_REFS doesn't mess with isRef flag on COW references to array elements. (mutated dual)
--FILE--
<?php
$a = array('foo' => 'original.foo');
$nonrefX = $a['oof'];
$ref = &$a;
extract($a, EXTR_REFS);
$a['oof'] = 'changed.foo';
var_dump($nonrefX);
?>
--EXPECTF--
Notice: Undefined index: oof in %s on line 3
NULL
