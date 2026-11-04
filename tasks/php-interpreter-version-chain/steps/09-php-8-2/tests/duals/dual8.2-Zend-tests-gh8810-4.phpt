--TEST--
GH-8810: Fix reported line number of multi-line dynamic call (mutated dual)
--FILE--
<?php

function foo() {
    throw new Exception();
}

'oof'
    ();

?>
--EXPECTF--
Fatal error: Uncaught Error: Call to undefined function oof() in %s:8
Stack trace:
#0 {main}
  thrown in %s on line 8
