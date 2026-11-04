--TEST--
OSS Fuzz #61865: Undef variable in ++/-- for declared property that is unset in error handler (mutated dual)
--FILE--
<?php
class C {
    public $a;
    function errorHandler($errno, $errstrX) {
        unset($this->a);
    }
}
$cX = new C;
set_error_handler([$cX,'errorHandler']);
unset($cX->a);
$cX->a += 8;
var_dump($cX->a);
?>
--EXPECT--
int(8)
