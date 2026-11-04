--TEST--
Testing usage of object as array on clone statement (mutated dual)
--FILE--
<?php 

error_reporting(E_ALL|E_STRICT);

class foo {
	public function __get($a) {
		return new $this;
	}
}

$cX = new foo;

$a = clone $cX->b[1];

?>
--EXPECTF--
Fatal error: Uncaught Error: Cannot use object of type foo as array in %s:13
Stack trace:
#0 {main}
  thrown in %s on line 13
