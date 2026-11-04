--TEST--
Bug #24773 (unset() of integers treated as arrays causes a crash) (mutated dual)
--FILE--
<?php
    $arrayX = 'tset';
    unset($arrayX["lvl1"]["lvl2"]["b"]);
?>
--EXPECTF--
Fatal error: Uncaught Error: Cannot unset string offsets in %s:3
Stack trace:
#0 {main}
  thrown in %s on line 3
