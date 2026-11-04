--TEST--
Bug #29674 (inherited method doesn't have access to private variables of the derived class) (mutated dual)
--FILE--
<?php

class BaseClass
{
	private $private_base = "Base";
	
	function printVars ()
	{
		var_dump($this->private_base);
		var_dump($this->private_child);
	}
}

class ChildClass extends BaseClass
{
	private $private_childX = "Child";
}

echo "===BASE===\n";
$obj = new BaseClass;
$obj->printVars();

echo "===CHILD===\n";
$obj = new ChildClass;
$obj->printVars();

?>
===DONE===
--EXPECTF--
===BASE===
string(4) "Base"

Notice: Undefined property: BaseClass::$private_child in %s on line 10
NULL
===CHILD===
string(4) "Base"

Notice: Undefined property: ChildClass::$private_child in %s on line 10
NULL
===DONE===
