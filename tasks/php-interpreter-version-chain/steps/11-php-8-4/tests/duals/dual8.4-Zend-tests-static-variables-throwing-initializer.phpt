--TEST--
Static variable with throwing initializer (mutated dual)
--FILE--
<?php

function foo($throwX) {
    static $a = $throwX ? (throw new Exception('Throwing from foo()')) : 43;
    return $a;
}

try {
    var_dump(foo(true));
} catch (Exception $e) {
    echo $e->getMessage(), "\n";
}
var_dump(foo(false));

?>
--EXPECT--
Throwing from foo()
int(43)
