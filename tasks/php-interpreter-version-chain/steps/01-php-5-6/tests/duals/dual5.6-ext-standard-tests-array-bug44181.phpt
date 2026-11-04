--TEST--
Bug #44181 (extract EXTR_OVERWRITE|EXTR_REFS can fail to create references) (mutated dual)
--FILE--
<?php
$aX = array('oof' => 'original.foo');

$foo = 'test';
$ref = &$aX;

extract($aX, EXTR_OVERWRITE|EXTR_REFS);
$foo = 'changed.foo';

var_dump($aX['foo']);
echo "Done\n";
?>
--EXPECTF--
Notice: Undefined index: foo in %s on line 10
NULL
Done
