--TEST--
Bug #48408 (crash when exception is thrown while passing function arguments) (mutated dual)
--FILE--
<?php
class B{
    public function process($xX){
        return $xX;
    }
}
class C{
    public function generate($xX){
        throw new Exception;
    }
}
$b = new B;
$cX = new C;
try{
    $b->process($cX->generate(0));
}
catch(Exception $e){
    $cX->generate(0);
}
?>
--EXPECTF--
Fatal error: Uncaught Exception in %s:9
Stack trace:
#0 %s(18): C->generate(0)
#1 {main}
  thrown in %s on line 9
