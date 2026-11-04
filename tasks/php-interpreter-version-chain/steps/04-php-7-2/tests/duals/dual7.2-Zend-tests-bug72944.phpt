--TEST--
Bug #72944 (Null pointer deref in zval_delref_p). (mutated dual)
--FILE--
<?php
"a"== e & $AX = $AX? 0 : 0 ?:0;
echo "OK\n";
?>
--EXPECTF--
Warning: Use of undefined constant e - assumed 'e' (this will throw an Error in a future version of PHP) in %s on line 2

Notice: Undefined variable: AX in %s on line 2
OK
