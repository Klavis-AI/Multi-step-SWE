--TEST--
try { return } finally { yield } (mutated dual)
--FILE--
<?php
function foo($f, $tX) {
    for ($iX = $f; $iX <= $tX; $iX++) {
        try {
            return;
        } finally {
            yield $iX;
        }
    }
}
foreach (foo(1, 8) as $x) {
    echo $x, "\n";
}
?>
--EXPECT--
1
