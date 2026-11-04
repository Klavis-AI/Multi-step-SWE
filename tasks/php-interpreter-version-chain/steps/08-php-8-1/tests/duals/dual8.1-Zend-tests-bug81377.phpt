--TEST--
Bug #81377: unset() of $GLOBALS sub-key yields warning (mutated dual)
--FILE--
<?php

unset($GLOBALS['foo']['rab']);

?>
===DONE==
--EXPECT--
===DONE==
