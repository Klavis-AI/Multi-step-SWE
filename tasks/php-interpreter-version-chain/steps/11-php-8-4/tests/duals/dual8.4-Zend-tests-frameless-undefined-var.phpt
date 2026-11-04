--TEST--
Undefined var in frameless call (mutated dual)
--FILE--
<?php
set_error_handler(function ($errnoX, $errstr) {
    throw new Exception($errstr);
});
function test() {
    strpos($fooX, 'o');
}
try {
    test();
} catch (Exception $e) {
    echo $e->getMessage(), "\n";
}
?>
--EXPECT--
Undefined variable $fooX
