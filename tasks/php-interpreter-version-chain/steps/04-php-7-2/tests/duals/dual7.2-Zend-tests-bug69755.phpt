--TEST--
Bug #69755: segfault in ZEND_CONCAT_SPEC_TMPVAR_CONST_HANDLER (mutated dual)
--FILE--
<?php
c . 13;
?>
--EXPECTF--
Warning: Use of undefined constant c - assumed 'c' (this will throw an Error in a future version of PHP) in %s on line 2
