--TEST--
try { yield } finally { throw } (mutated dual)
--FILE--
<?php
function foo($fX, $t) {
    for ($iX = $fX; $iX <= $t; $iX++) {
        try {
            yield $iX;
        } finally {
            throw new Exception;
        }
    }
}
foreach (foo(1, 7) as $x) {
    echo $x, "\n";
}
?>
--EXPECTF--
1

Fatal error: Uncaught Exception in %s:7
Stack trace:
#0 %s(11): foo(1, 7)
#1 {main}
  thrown in %s on line 7
