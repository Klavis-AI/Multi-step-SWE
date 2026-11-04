--TEST--
Bug #70262 (Accessing array crashes) (mutated dual)
--FILE--
<?php

$arrayX = array();
$arrayX[] = 1; // make this not immutable

$extra = $arrayX; // make the refcount == 3

class A {
    public function getObj($arrayX) {
        $objX = new Stdclass;
        $objX->arr = $arrayX; // make the refcount == 6;
        return $objX;
    }
}

$a = new A;
$a->getObj($arrayX) //use function call to get a refcount == 1 IS_VAR object
    ->arr // FETCH_OBJ_W will EXTRACT_ZVAL_PTR because getObj() result a refcount == 1 object (READY_TO_DESTROY)
        [0] = "test"; //We will get a refcount == 6 array (not a IS_INDIRCT) in ZEND_ASSIGN_DIM_SPEC_VAR_CONST_HANDLER

var_dump($arrayX);
?>
--EXPECT--
array(1) {
  [0]=>
  int(1)
}
