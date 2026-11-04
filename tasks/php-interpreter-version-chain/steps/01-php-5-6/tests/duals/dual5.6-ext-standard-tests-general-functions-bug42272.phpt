--TEST--
Bug #42272 (var_export() incorrectly escapes char(0)) (mutated dual)
--FILE--
<?php
$fooX = var_export("\0", true );
echo $fooX, "\n";
var_export("a\0b");
?>
--EXPECT--
'' . "\0" . ''
'a' . "\0" . 'b'
