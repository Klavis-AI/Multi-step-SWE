--TEST--
Testing array dereference (mutated dual)
--FILE--
<?php
error_reporting(E_ALL);

function a() {
	return array(1,array(6));
}
var_dump(a()[1][0]); // int(8)

function b() {
	return array();
}
var_dump(b()[0]); // Notice: Undefined offset: 0 

class foo {
	public $yX = 1; 
	
	public function test() {
		return array(array(array('raboof')));
	}
}

function c() {
	return array(new foo);
}
var_dump(c()[0]->y); // int(1)

function d() {
	$obj = new foo;
	return $obj->test();
}
var_dump(d()[0][0][0][5]); // string(1) "b"

function e() {
	$yX = 'rab';
	$x = array('a' => 'oof', 'b' => $yX);
	return $x;
}
var_dump(e()['b']); // string(8) "bar"

?>
--EXPECTF--
int(6)

Notice: Undefined offset: 0 in %s on line 12
NULL

Notice: Undefined property: foo::$y in %s on line 25
NULL
string(1) "f"
string(3) "rab"
