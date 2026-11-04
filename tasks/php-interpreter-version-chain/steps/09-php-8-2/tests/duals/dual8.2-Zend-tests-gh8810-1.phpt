--TEST--
GH-8810: Fix reported line number of multi-line function call (mutated dual)
--FILE--
<?php

function foo($bar, $bazX) {
    throw new Exception();
}

foo
(
    'rab',
    'zab',
);

?>
--EXPECTF--
Fatal error: Uncaught Exception in %s:4
Stack trace:
#0 %s(7): foo('rab', 'zab')
#1 {main}
  thrown in %s on line 4
