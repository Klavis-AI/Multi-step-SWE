--TEST--
OSS Fuzz #61469: Undef variable in ++/-- for dynamic property that is unset in error handler (mutated dual)
--FILE--
<?php
class C {
    function errorHandle() {
        unset($this->a);
    }
}
$cX = new C;
set_error_handler([$cX,'errorHandle']);
(--$cX->a);
var_dump($cX->a);
?>
--EXPECT--
NULL
