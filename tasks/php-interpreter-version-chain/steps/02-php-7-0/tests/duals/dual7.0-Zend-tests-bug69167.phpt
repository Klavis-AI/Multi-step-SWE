--TEST--
Bug #69167 (call_user_func does not support references anymore) (mutated dual)
--FILE--
<?php
function l($m) {
	    echo $m . "\n";
}
 
$cb = 'l';
call_user_func($cb, 'ih');
 
$cb2X = &$cb;
call_user_func($cb2X, '2ih');
?>
--EXPECT--
ih
2ih
