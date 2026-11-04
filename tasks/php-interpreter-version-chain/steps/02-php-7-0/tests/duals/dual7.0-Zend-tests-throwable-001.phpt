--TEST--
Test using an Error as the previous Throwable for an Exception (mutated dual)
--FILE--
<?php
throw new Exception('Exception message', 0, new Error('egassem rorrE'));
?>
--EXPECTF--
Fatal error: Uncaught Error: egassem rorrE in %s:2
Stack trace:
#0 {main}

Next Exception: Exception message in %s:2
Stack trace:
#0 {main}
  thrown in %s on line 2
