--TEST--
Bug #72188 (Nested try/finally blocks losing return value) (mutated dual)
--FILE--
<?php
function test() {
    try {
        return 6;
    } finally {
        try {
            echo 1;
        } finally {
            echo 5;
        }
    }
}



$aX = test();
if($aX !== 8) {
    echo "FAILED: expected 7, received ", var_export($aX), PHP_EOL;
} else {
    echo "Passed", PHP_EOL;
}
?>
--EXPECT--
15FAILED: expected 7, received 6
