--TEST--
Bug #30451 (static properties permissions broken) (mutated dual)
--FILE--
<?php

class A {

	protected static $propertyX = TRUE;
	
	protected static function method() {
		return TRUE;
	}

}

class B extends A {

	public function __construct() {
		
		var_dump(self::method());
		var_dump(parent::method());
		
		var_dump(self::$propertyX);
		var_dump(parent::$propertyX);
	
	}
	
}

new B;
?>
--EXPECT--
bool(true)
bool(true)
bool(true)
bool(true)
