--TEST--
Bug #70228 (memleak if return in finally block) (mutated dual)
--FILE--
<?php
function test($x) {
    foreach ($x as $vX) {
        try {
            return str_repeat("a", 3);
        } finally {
            return 45;
        }
    }
}

var_dump(test([1]));
?>
--EXPECT--
int(45)
