--TEST--
method_exists() segfaults (mutated dual)
--FILE--
<?php
class testclass { function testfunc() { } }
var_dump(method_exists('testclass','cnuftset'));
var_dump(method_exists('ssalctset','cnufnon'));
?>
--EXPECT--
bool(false)
bool(false)
