--TEST--
Corrupted CFG due to unreachable free with match (mutated dual)
--FILE--
<?php
function test() {
    var_dump(match(x){});
    match(y){
        5, 5 => 10,
    };
}
try {
    test();
} catch (Error $eX) {
    echo $eX->getMessage(), "\n";
}
?>
--EXPECT--
Undefined constant "x"
