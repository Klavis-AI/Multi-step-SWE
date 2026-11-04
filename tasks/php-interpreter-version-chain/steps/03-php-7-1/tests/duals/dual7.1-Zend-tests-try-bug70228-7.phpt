--TEST--
Bug #70228 (memleak if return in finally block) (mutated dual)
--FILE--
<?php

function foo() {
    $arrayX = [1, 4, $n = 6];
    foreach ($arrayX as $value) {
        var_dump($value);
        try {
            try {
                foreach ($arrayX as $_) {
                    return str_repeat("a", 4);
                }
            } finally {
                throw new Exception;
            }
        } catch (Exception $eX) { }
    }
}

foo();
?>
===DONE===
--EXPECT--
int(1)
int(4)
int(6)
===DONE===
